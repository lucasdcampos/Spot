#include "core/assert.h"
#include "core/log.h"
#include "core/paths.h"
#include "scene/archive.h"
#include "scene/archive_state.h"
#include "scene/component_registry.h"
#include "scene/component_serialization.h"
#include "scene/missing_component.h"
#include "scene/scene.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace Spot {

using nlohmann::json;

namespace {

constexpr std::string_view Format = "spot-scene";
constexpr int FormatVersion = 1;

json MakeDocument() {
    return {{"format", Format}, {"version", FormatVersion}, {"entities", json::array()}};
}

std::string Dump(const json& document) {
    // Invalid UTF-8 in names or fields is replaced rather than failing the whole save.
    return document.dump(2, ' ', false, json::error_handler_t::replace);
}

// Unlike json::value(), these never throw when the field has another type.
std::string GetString(const json& object, std::string_view key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

bool GetBool(const json& object, std::string_view key, bool fallback) {
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() ? found->get<bool>() : fallback;
}

std::int64_t GetInteger(const json& object, std::string_view key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number_integer() ? found->get<std::int64_t>() : 0;
}

} // namespace

namespace Detail {

struct SceneSerializer {
    using IdMap = std::unordered_map<std::uint64_t, EntityId>;

    static void Serialize(Component& component, Archive& archive) { component.Serialize(archive); }
    static void Validate(Component& component) { component.OnValidate(); }

    // Saves the entity and then its descendants, depth first, so parents always come before their children and
    // children keep their order.
    static void SaveTree(const Entity& entity, json& entities) {
        SaveEntity(entity, entities.emplace_back(json::object()));
        for (const Entity* child : entity.m_children) {
            if (!child->m_destroyed) {
                SaveTree(*child, entities);
            }
        }
    }

    static void SaveEntity(const Entity& entity, json& node) {
        ArchiveState state{.loading = false, .nodes = {&node}};
        Archive archive(state);

        auto id = static_cast<std::uint64_t>(entity.m_id);
        archive.Field("id", id);
        std::string name = entity.m_name;
        archive.Field("name", name);
        bool active = entity.m_activeSelf;
        archive.Field("active", active);
        if (entity.m_parent) {
            EntityId parent = entity.m_parent->m_id;
            archive.Field("parent", parent);
        }
        std::vector<std::string> tags = entity.m_tags;
        archive.Field("tags", tags);

        const Transform& transform = entity.m_transform;
        Vector3 position = transform.GetPosition();
        Quaternion rotation = transform.GetRotation();
        Vector3 scale = transform.GetScale();
        archive.Field("position", position);
        archive.Field("rotation", rotation);
        archive.Field("scale", scale);

        json& components = node["components"] = json::array();
        for (const auto& component : entity.m_components) {
            if (component->m_removed) {
                continue;
            }
            if (auto saved = SaveComponent(*component)) {
                components.push_back(std::move(*saved));
            } else {
                Log::GetEngineLogger().Warning("A component of entity '{}' is not saved: its type {} is not registered",
                                               entity.m_name, typeid(*component).name());
            }
        }
    }

    // The component's type, enabled state and fields; nothing when its type is not registered.
    static std::optional<json> SaveComponent(const Component& component) {
        json saved = json::object();
        if (const auto* missing = dynamic_cast<const MissingComponent*>(&component)) {
            // Kept exactly as it was loaded, since nothing here knows its fields.
            saved["type"] = missing->m_typeName;
            saved["enabled"] = component.m_enabled;
            saved["data"] = json::parse(missing->m_data, nullptr, false);
            if (!saved["data"].is_object()) {
                saved["data"] = json::object();
            }
            return saved;
        }
        const std::string* type = FindComponentTypeName(typeid(component));
        if (!type) {
            return std::nullopt;
        }
        saved["type"] = *type;
        saved["enabled"] = component.m_enabled;
        json& data = saved["data"] = json::object();
        ArchiveState state{.loading = false, .nodes = {&data}};
        Archive archive(state);
        // Serialize() both saves and loads, so it cannot be const.
        const_cast<Component&>(component).Serialize(archive);
        return saved;
    }

    static std::optional<json> ParseDocument(std::string_view text) {
        json document = json::parse(text.begin(), text.end(), nullptr, false);
        if (document.is_discarded()) {
            Log::GetEngineLogger().Warning("Could not load the scene: the text is not valid JSON");
            return std::nullopt;
        }
        if (!document.is_object() || GetString(document, "format") != Format) {
            Log::GetEngineLogger().Warning("Could not load the scene: the text is not a saved scene");
            return std::nullopt;
        }
        if (const std::int64_t version = GetInteger(document, "version"); version != FormatVersion) {
            Log::GetEngineLogger().Warning("Could not load the scene: version {} of the format is not supported",
                                           version);
            return std::nullopt;
        }
        const auto entities = document.find("entities");
        if (entities == document.end() || !entities->is_array() || !std::ranges::all_of(*entities, &json::is_object)) {
            Log::GetEngineLogger().Warning("Could not load the scene: its entities are not a list of objects");
            return std::nullopt;
        }
        return document;
    }

    static std::optional<std::vector<Entity*>> Load(Scene& scene, std::string_view text,
                                                    const InstantiateOptions& options) {
        std::optional<json> document = ParseDocument(text);
        if (!document) {
            return std::nullopt;
        }
        json& entities = (*document)["entities"];

        const DeferRemovalsScope defer(scene);

        // Every entity is created first, so references between them can be mapped to the new ids.
        IdMap ids;
        std::vector<Entity*> created;
        created.reserve(entities.size());
        for (json& node : entities) {
            ArchiveState state{.loading = true, .nodes = {&node}};
            Archive archive(state);

            std::string name;
            archive.Field("name", name);
            std::uint64_t id = 0;
            archive.Field("id", id);
            Entity& entity = options.keepIds ? scene.CreateEntityWithId(static_cast<EntityId>(id), std::move(name))
                                             : scene.CreateEntity(std::move(name));
            created.push_back(&entity);
            if (id != 0) {
                ids.insert_or_assign(id, entity.m_id);
            }
            // No component is attached yet, so the activity can be set without any hook to call.
            archive.Field("active", entity.m_activeSelf);
            std::vector<std::string> tags;
            archive.Field("tags", tags);
            for (const std::string& tag : tags) {
                entity.AddTag(tag);
            }

            Transform& transform = entity.m_transform;
            Vector3 position = transform.GetPosition();
            Quaternion rotation = transform.GetRotation();
            Vector3 scale = transform.GetScale();
            archive.Field("position", position);
            archive.Field("rotation", rotation);
            archive.Field("scale", scale);
            transform.SetPosition(position);
            transform.SetRotation(rotation);
            transform.SetScale(scale);
        }

        const Scene* referenced = options.keepExternalReferences ? &scene : nullptr;
        for (std::size_t i = 0; i < created.size(); ++i) {
            Entity& entity = *created[i];
            // Parents only come from the saved entities, so a saved subtree loads as a new one wherever it came from.
            ArchiveState parentState{.loading = true, .nodes = {&entities[i]}, .entityIds = &ids};
            Archive parentArchive(parentState);
            EntityId parentId = EntityId::Invalid;
            parentArchive.Field("parent", parentId);
            Entity* parent = scene.GetEntity(parentId);
            if (parent && parent != &entity && !parent->IsDescendantOf(entity)) {
                entity.SetParent(parent);
            }

            const auto components = entities[i].find("components");
            if (components != entities[i].end() && components->is_array()) {
                for (json& saved : *components) {
                    LoadComponent(entity, saved, std::nullopt, ids, referenced, options.assets);
                }
            }
        }

        std::vector<Entity*> roots;
        for (Entity* entity : created) {
            if (!entity->m_parent) {
                roots.push_back(entity);
            }
        }
        for (std::size_t i = 0; i < roots.size(); ++i) {
            if (options.parent) {
                roots[i]->SetParent(options.parent);
            }
            if (options.siblingIndex) {
                roots[i]->SetSiblingIndex(*options.siblingIndex + i);
            }
        }
        return roots;
    }

    static Component* LoadComponent(Entity& entity, json& saved, std::optional<std::size_t> index, const IdMap& ids,
                                    const Scene* referenced, AssetManager* assets) {
        if (!saved.is_object()) {
            return nullptr;
        }
        const std::string type = GetString(saved, "type");
        const auto data = saved.find("data");
        const bool hasData = data != saved.end() && data->is_object();

        std::unique_ptr<Component> owned;
        if (const ComponentFactory factory = FindComponentFactory(type)) {
            owned = factory();
        } else {
            Log::GetEngineLogger().Warning(
                "A component of entity '{}' is of type '{}', which is not registered; its data is kept as it is",
                entity.m_name, type);
            owned = std::make_unique<MissingComponent>(type, hasData ? data->dump() : "{}");
        }
        Component& component = *owned;
        component.m_enabled = GetBool(saved, "enabled", true);
        entity.Attach(std::move(owned), index);

        if (hasData && !dynamic_cast<MissingComponent*>(&component)) {
            ArchiveState state{
                .loading = true, .nodes = {&*data}, .entityIds = &ids, .scene = referenced, .assets = assets};
            Archive archive(state);
            component.Serialize(archive);
            component.OnValidate();
        }
        return &component;
    }

    static bool ReplaceComponent(Component& component, std::string_view text, AssetManager* assets) {
        json saved = json::parse(text.begin(), text.end(), nullptr, false);
        if (!saved.is_object()) {
            Log::GetEngineLogger().Warning("Could not load the component: the text is not a saved component");
            return false;
        }
        const std::string type = GetString(saved, "type");
        auto* missing = dynamic_cast<MissingComponent*>(&component);
        const std::string* registered = FindComponentTypeName(typeid(component));
        const std::string_view expected = missing      ? std::string_view(missing->m_typeName)
                                          : registered ? std::string_view(*registered)
                                                       : std::string_view();
        if (expected.empty() || type != expected) {
            Log::GetEngineLogger().Warning("Could not load the component: the text is for a '{}', not a '{}'", type,
                                           expected);
            return false;
        }

        const auto data = saved.find("data");
        if (missing) {
            missing->m_data = data != saved.end() && data->is_object() ? data->dump() : "{}";
        } else if (data != saved.end() && data->is_object()) {
            // Ids load as saved: the text comes from the same scene, such as an earlier state of the component.
            ArchiveState state{.loading = true, .nodes = {&*data}, .assets = assets};
            Archive archive(state);
            component.Serialize(archive);
            component.OnValidate();
        }
        component.SetEnabled(GetBool(saved, "enabled", component.m_enabled));
        return true;
    }
};

} // namespace Detail

std::string Scene::SaveToString() const {
    json document = MakeDocument();
    for (const Entity* root : m_roots) {
        if (!root->m_destroyed) {
            Detail::SceneSerializer::SaveTree(*root, document["entities"]);
        }
    }
    return Dump(document);
}

std::string Scene::SaveEntitiesToString(std::span<Entity* const> entities) const {
    json document = MakeDocument();
    for (const Entity* entity : entities) {
        SPOT_ASSERT(&entity->GetScene() == this, "The entity belongs to another scene");
        const bool savedWithAncestor = std::ranges::any_of(
            entities, [entity](const Entity* other) { return other != entity && entity->IsDescendantOf(*other); });
        if (!entity->m_destroyed && !savedWithAncestor) {
            Detail::SceneSerializer::SaveTree(*entity, document["entities"]);
        }
    }
    return Dump(document);
}

bool Scene::SaveToFile(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file || !(file << SaveToString())) {
        Log::GetEngineLogger().Warning("Could not save the scene to '{}'", PathToUtf8(path));
        return false;
    }
    return true;
}

bool Scene::LoadFromString(std::string_view text, AssetManager* assets) {
    return Detail::SceneSerializer::Load(*this, text, {.assets = assets}).has_value();
}

bool Scene::LoadFromFile(const std::filesystem::path& path, AssetManager* assets) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Log::GetEngineLogger().Warning("Could not open the scene file '{}'", PathToUtf8(path));
        return false;
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return LoadFromString(text, assets);
}

std::vector<Entity*> Scene::Instantiate(std::string_view text, const InstantiateOptions& options) {
    SPOT_ASSERT(!options.parent || &options.parent->GetScene() == this, "The parent belongs to another scene");
    return Detail::SceneSerializer::Load(*this, text, options).value_or(std::vector<Entity*>());
}

std::string Entity::SaveToString() const {
    SPOT_ASSERT(!m_destroyed, "A destroyed entity cannot be saved");
    json document = MakeDocument();
    Detail::SceneSerializer::SaveTree(*this, document["entities"]);
    return Dump(document);
}

void SerializeComponent(Component& component, Archive& archive) {
    Detail::SceneSerializer::Serialize(component, archive);
}

void ValidateComponent(Component& component) {
    Detail::SceneSerializer::Validate(component);
}

std::string SaveComponentToString(const Component& component) {
    const auto saved = Detail::SceneSerializer::SaveComponent(component);
    return saved ? Dump(*saved) : std::string();
}

bool LoadComponentFromString(Component& component, std::string_view text, AssetManager* assets) {
    return Detail::SceneSerializer::ReplaceComponent(component, text, assets);
}

Component* AddComponentFromString(Entity& entity, std::string_view text, std::optional<std::size_t> index,
                                  AssetManager* assets) {
    json saved = json::parse(text.begin(), text.end(), nullptr, false);
    if (!saved.is_object() || GetString(saved, "type").empty()) {
        Log::GetEngineLogger().Warning("Could not add the component: the text is not a saved component");
        return nullptr;
    }
    const Detail::SceneSerializer::IdMap noIds;
    // References keep their ids: the text comes from the same scene, such as a component removed and brought back.
    return Detail::SceneSerializer::LoadComponent(entity, saved, index, noIds, &entity.GetScene(), assets);
}

} // namespace Spot
