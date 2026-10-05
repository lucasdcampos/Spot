#include "core/assert.h"
#include "core/log.h"
#include "core/paths.h"
#include "scene/archive.h"
#include "scene/archive_state.h"
#include "scene/component_registry.h"
#include "scene/scene.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
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
            const std::string* type = FindComponentTypeName(typeid(*component));
            if (!type) {
                Log::GetEngineLogger().Warning("A component of entity '{}' is not saved: its type {} is not registered",
                                               entity.m_name, typeid(*component).name());
                continue;
            }
            json& saved = components.emplace_back(json::object());
            saved["type"] = *type;
            saved["enabled"] = component->m_enabled;
            json& data = saved["data"] = json::object();
            ArchiveState dataState{.loading = false, .nodes = {&data}};
            Archive dataArchive(dataState);
            // Serialize() both saves and loads, so it cannot be const.
            const_cast<Component&>(*component).Serialize(dataArchive);
        }
    }

    static bool Load(Scene& scene, std::string_view text, AssetManager* assets) {
        json document = json::parse(text.begin(), text.end(), nullptr, false);
        if (document.is_discarded()) {
            Log::GetEngineLogger().Warning("Could not load the scene: the text is not valid JSON");
            return false;
        }
        if (!document.is_object() || GetString(document, "format") != Format) {
            Log::GetEngineLogger().Warning("Could not load the scene: the text is not a saved scene");
            return false;
        }
        if (const std::int64_t version = GetInteger(document, "version"); version != FormatVersion) {
            Log::GetEngineLogger().Warning("Could not load the scene: version {} of the format is not supported",
                                           version);
            return false;
        }
        json& entities = document["entities"];
        if (!entities.is_array() || !std::ranges::all_of(entities, &json::is_object)) {
            Log::GetEngineLogger().Warning("Could not load the scene: its entities are not a list of objects");
            return false;
        }

        const DeferRemovalsScope defer(scene);

        // Every entity is created first, so references between them can be mapped to the new ids.
        std::unordered_map<std::uint64_t, EntityId> ids;
        std::vector<Entity*> created;
        created.reserve(entities.size());
        for (json& node : entities) {
            ArchiveState state{.loading = true, .nodes = {&node}};
            Archive archive(state);

            std::string name;
            archive.Field("name", name);
            Entity& entity = scene.CreateEntity(std::move(name));
            created.push_back(&entity);

            std::uint64_t id = 0;
            archive.Field("id", id);
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

        for (std::size_t i = 0; i < created.size(); ++i) {
            Entity& entity = *created[i];
            ArchiveState state{.loading = true, .nodes = {&entities[i]}, .entityIds = &ids};
            Archive archive(state);

            EntityId parentId = EntityId::Invalid;
            archive.Field("parent", parentId);
            Entity* parent = scene.GetEntity(parentId);
            if (parent && parent != &entity && !parent->IsDescendantOf(entity)) {
                entity.SetParent(parent);
            }

            const auto components = entities[i].find("components");
            if (components != entities[i].end() && components->is_array()) {
                for (json& saved : *components) {
                    LoadComponent(entity, saved, ids, assets);
                }
            }
        }
        return true;
    }

    static void LoadComponent(Entity& entity, json& saved, const std::unordered_map<std::uint64_t, EntityId>& ids,
                              AssetManager* assets) {
        if (!saved.is_object()) {
            return;
        }
        const std::string type = GetString(saved, "type");
        const ComponentFactory factory = FindComponentFactory(type);
        if (!factory) {
            Log::GetEngineLogger().Warning("A component of entity '{}' is not loaded: its type '{}' is not registered",
                                           entity.m_name, type);
            return;
        }

        std::unique_ptr<Component> owned = factory();
        Component& component = *owned;
        component.m_enabled = GetBool(saved, "enabled", true);
        entity.Attach(std::move(owned));

        if (const auto data = saved.find("data"); data != saved.end() && data->is_object()) {
            ArchiveState state{.loading = true, .nodes = {&*data}, .entityIds = &ids, .assets = assets};
            Archive archive(state);
            component.Serialize(archive);
        }
    }
};

} // namespace Detail

std::string Scene::SaveToString() const {
    json document = MakeDocument();
    for (const auto& entity : m_entities) {
        if (IsAlive(entity) && !entity->m_parent) {
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
    return Detail::SceneSerializer::Load(*this, text, assets);
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

std::string Entity::SaveToString() const {
    SPOT_ASSERT(!m_destroyed, "A destroyed entity cannot be saved");
    json document = MakeDocument();
    Detail::SceneSerializer::SaveTree(*this, document["entities"]);
    return Dump(document);
}

} // namespace Spot
