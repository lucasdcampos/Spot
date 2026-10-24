#include "spot/assets/asset_archive.h"
#include "spot/assets/assets.h"
#include "spot/core/debug/assert.h"
#include "spot/core/debug/log.h"
#include "spot/core/io/archive.h"
#include "spot/core/io/archive_state.h"
#include "spot/core/io/paths.h"
#include "spot/scene/component_registry.h"
#include "spot/scene/component_serialization.h"
#include "spot/scene/missing_component.h"
#include "spot/scene/prefab_document.h"
#include "spot/scene/scene.h"
#include "spot/scene/scene_serializer.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace Spot {

using nlohmann::json;

namespace {

using IdMap = Detail::SceneSerializer::IdMap;

// Entity references in scene files: plain ids, remapped to the new ids of the entities loaded along, or, inside a
// prefab instance, marked as references to the instance's entities or to the others (see prefab_document.h).
class SceneEntityReferences final : public Detail::EntityReferenceCodec {
public:
    explicit SceneEntityReferences(const IdMap* entityIds = nullptr, const Scene* scene = nullptr,
                                   const IdMap* prefabIds = nullptr, const IdMap* localEntityIds = nullptr,
                                   const IdMap* externalEntityIds = nullptr)
        : entityIds(entityIds), scene(scene), prefabIds(prefabIds), localEntityIds(localEntityIds),
          externalEntityIds(externalEntityIds) {}

    // When loading, maps the saved ids of the entities being loaded to their new ids. Without it, ids load as saved.
    const IdMap* entityIds = nullptr;
    // With entityIds, ids missing from them that this scene has an entity for are kept rather than lost.
    const Scene* scene = nullptr;
    // Saving an instance's entity: maps the ids of the instance's entities to their ids in the prefab, saved as
    // markers. Other ids are saved as scene references.
    const IdMap* prefabIds = nullptr;
    // Loading markers: references inside the instance map through the first, the others through the second.
    const IdMap* localEntityIds = nullptr;
    const IdMap* externalEntityIds = nullptr;

    void Field(Detail::ArchiveState& state, std::string_view name, EntityId& value) const override {
        auto id = static_cast<std::uint64_t>(value);
        if (!state.loading) {
            json& field = Detail::AddArchiveField(state, name);
            if (prefabIds) {
                const auto found = prefabIds->find(id);
                field = found != prefabIds->end() ? json{{"prefabEntity", static_cast<std::uint64_t>(found->second)}}
                                                  : json{{"sceneEntity", id}};
            } else {
                field = id;
            }
            return;
        }
        const json* field = Detail::FindArchiveField(state, name);
        if (!field) {
            return;
        }
        if (field->is_object()) {
            const auto local = field->find("prefabEntity");
            const auto external = field->find("sceneEntity");
            const json* saved = local != field->end() ? &*local : external != field->end() ? &*external : nullptr;
            if (!saved || field->size() != 1 || !saved->is_number_unsigned()) {
                Detail::WarnArchiveFieldType(name, "an entity reference");
                return;
            }
            id = saved->get<std::uint64_t>();
            const IdMap* ids = local != field->end() ? localEntityIds : externalEntityIds;
            if (ids) {
                const auto found = ids->find(id);
                value = found != ids->end() ? found->second : EntityId::Invalid;
            } else if (local != field->end()) {
                value = EntityId::Invalid;
            } else {
                value = static_cast<EntityId>(id);
            }
            if (external != field->end() && value == EntityId::Invalid && scene &&
                scene->GetEntity(static_cast<EntityId>(id))) {
                value = static_cast<EntityId>(id);
            }
            return;
        }
        if (!field->is_number_unsigned()) {
            Detail::WarnArchiveFieldType(name, "an unsigned integer");
            return;
        }
        id = field->get<std::uint64_t>();
        if (!entityIds) {
            value = static_cast<EntityId>(id);
            return;
        }
        const auto found = entityIds->find(id);
        if (found != entityIds->end()) {
            value = found->second;
        } else if (scene && scene->GetEntity(static_cast<EntityId>(id))) {
            value = static_cast<EntityId>(id);
        } else {
            value = EntityId::Invalid;
        }
    }
};

constexpr std::string_view Format = "spot-scene";
constexpr int FormatVersion = 2;

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

namespace {

// Components of unregistered types, gathered while a scene or prefab loads so each type is reported once: a game
// whose code is not loaded yet would otherwise log a line per component.
struct UnregisteredComponents {
    struct Type {
        std::size_t count = 0;
        std::vector<std::string> entities;
        bool kept = false;
    };
    std::map<std::string, Type> types;
};
thread_local UnregisteredComponents* t_unregistered = nullptr;

class UnregisteredScope {
public:
    UnregisteredScope() : m_owner(!t_unregistered) {
        if (m_owner)
            t_unregistered = &m_components;
    }
    ~UnregisteredScope() {
        if (!m_owner)
            return;
        t_unregistered = nullptr;
        for (const auto& [type, found] : m_components.types) {
            std::string names;
            constexpr std::size_t Listed = 3;
            for (std::size_t i = 0; i < std::min(Listed, found.entities.size()); ++i)
                names += (i == 0 ? "'" : ", '") + found.entities[i] + "'";
            if (found.entities.size() > Listed)
                names += " and " + std::to_string(found.entities.size() - Listed) + " more";
            Log::GetEngineLogger().Warning("{} component{} of type '{}', which is not registered, {} (on {})",
                                           found.count, found.count == 1 ? "" : "s", type,
                                           found.kept ? "keep their data as it is" : "are skipped", names);
        }
    }
    UnregisteredScope(const UnregisteredScope&) = delete;
    UnregisteredScope& operator=(const UnregisteredScope&) = delete;

private:
    bool m_owner;
    UnregisteredComponents m_components;
};

void ReportUnregistered(const std::string& entity, const std::string& type, bool kept) {
    if (!t_unregistered) {
        Log::GetEngineLogger().Warning("A component of entity '{}' is of type '{}', which is not registered; {}",
                                       entity, type, kept ? "its data is kept as it is" : "it is skipped");
        return;
    }
    auto& found = t_unregistered->types[type];
    ++found.count;
    found.kept = kept;
    if (std::ranges::find(found.entities, entity) == found.entities.end())
        found.entities.push_back(entity);
}

} // namespace

namespace Detail {

void SceneSerializer::Serialize(Component& component, Archive& archive) {
    component.Serialize(archive);
}
void SceneSerializer::Validate(Component& component) {
    component.InvokeHook("OnValidate", [&component] { component.OnValidate(); });
}

// Saves the entity and then its descendants, depth first, so parents always come before their children and
// children keep their order.
void SceneSerializer::SaveTree(const Entity& entity, json& entities, const PrefabRoots& roots,
                               std::unordered_map<EntityId, IdMap>& localIds) {
    json& saved = entities.emplace_back(json::object());
    if (roots.contains(entity.m_prefabRoot)) {
        auto [instance, added] = localIds.try_emplace(entity.m_prefabRoot);
        if (added) {
            std::vector<const Entity*> pending{entity.m_scene->GetEntity(entity.m_prefabRoot)};
            while (!pending.empty()) {
                const Entity* member = pending.back();
                pending.pop_back();
                if (member->m_prefabRoot == entity.m_prefabRoot) {
                    instance->second[static_cast<std::uint64_t>(member->m_id)] =
                        static_cast<EntityId>(member->m_prefabLocalId);
                }
                pending.insert(pending.end(), member->m_children.begin(), member->m_children.end());
            }
        }
        SaveEntity(entity, saved, &instance->second);
        saved["prefabRoot"] = static_cast<std::uint64_t>(entity.m_prefabRoot);
        saved["prefabLocal"] = entity.m_prefabLocalId;
        if (entity.m_prefabRoot == entity.m_id) {
            saved["prefabAsset"] = entity.m_prefabAsset.GetId().ToString();
        }
    } else {
        SaveEntity(entity, saved);
    }
    for (const Entity* child : entity.m_children) {
        if (!child->m_destroyed) {
            SaveTree(*child, entities, roots, localIds);
        }
    }
}

void SceneSerializer::SaveEntity(const Entity& entity, json& node, const IdMap* prefabIds) {
    const SceneEntityReferences references(nullptr, nullptr, prefabIds);
    ArchiveState state{.loading = false, .nodes = {&node}, .entities = &references};
    Archive archive(state);

    auto id = static_cast<std::uint64_t>(entity.m_id);
    archive.Field("id", id);
    std::string name = entity.m_name;
    archive.Field("name", name);
    bool active = entity.m_activeSelf;
    archive.Field("active", active);
    if (entity.m_parent) {
        // Parents are plain ids even inside prefab instances, as the hierarchy is the document's own.
        node["parent"] = static_cast<std::uint64_t>(entity.m_parent->m_id);
    }
    std::vector<std::string> tags = entity.m_tags;
    archive.Field("tags", tags);
    int layer = entity.m_layer;
    archive.Field("layer", layer);

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
        if (auto saved = SceneSerializer::SaveComponent(*component, prefabIds)) {
            components.push_back(std::move(*saved));
        } else {
            Log::GetEngineLogger().Warning("A component of entity '{}' is not saved: its type {} is not registered",
                                           entity.m_name, typeid(*component).name());
        }
    }
}

// The component's type, enabled state and fields; nothing when its type is not registered.
std::optional<json> SceneSerializer::SaveComponent(const Component& component, const IdMap* prefabIds) {
    json saved = {{"id", component.m_id.ToString()}};
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
    const SceneEntityReferences references(nullptr, nullptr, prefabIds);
    ArchiveState state{.loading = false, .nodes = {&data}, .entities = &references};
    Archive archive(state);
    // Serialize() both saves and loads, so it cannot be const.
    const_cast<Component&>(component).Serialize(archive);
    return saved;
}

std::optional<json> SceneSerializer::ParseDocument(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (document.is_discarded()) {
        Log::GetEngineLogger().Warning("Could not load the scene: the text is not valid JSON");
        return std::nullopt;
    }
    if (!document.is_object() || GetString(document, "format") != Format) {
        Log::GetEngineLogger().Warning("Could not load the scene: the text is not a saved scene");
        return std::nullopt;
    }
    if (const std::int64_t version = GetInteger(document, "version"); version != 1 && version != FormatVersion) {
        Log::GetEngineLogger().Warning("Could not load the scene: version {} of the format is not supported", version);
        return std::nullopt;
    }
    const auto entities = document.find("entities");
    if (entities == document.end() || !entities->is_array() || !std::ranges::all_of(*entities, &json::is_object)) {
        Log::GetEngineLogger().Warning("Could not load the scene: its entities are not a list of objects");
        return std::nullopt;
    }
    return document;
}

std::optional<std::vector<Entity*>> SceneSerializer::Load(Scene& scene, std::string_view text,
                                                          const InstantiateOptions& options) {
    std::optional<json> document = ParseDocument(text);
    if (!document) {
        return std::nullopt;
    }
    return Load(scene, std::move(*document), options);
}

std::optional<std::vector<Entity*>> SceneSerializer::Load(Scene& scene, json document,
                                                          const InstantiateOptions& options,
                                                          const Prefab::NestedPrefabs* prefabs) {
    const UnregisteredScope unregistered;
    AssetManager* manager = options.assets ? options.assets : Assets::GetManager();
    const Prefab::NestedPrefabs managed = [manager](AssetId id) {
        return manager ? manager->Load<Prefab>(id) : AssetRef<Prefab>();
    };
    PrefabAssets prefabAssets;
    if (!ExpandPrefabs(document, prefabs ? *prefabs : managed, &prefabAssets)) {
        Log::GetEngineLogger().Warning("Could not load the scene: its prefab instances could not be resolved");
        return std::nullopt;
    }
    json& entities = document["entities"];
    const json noScopes = json::array();
    const auto savedScopes = document.find("referenceScopes");
    const json& scopes = savedScopes != document.end() && savedScopes->is_array() ? *savedScopes : noScopes;

    const DeferRemovalsScope defer(scene);

    // Every entity is created first, so references between them can be mapped to the new ids.
    IdMap ids;
    std::vector<Entity*> created;
    created.reserve(entities.size());
    for (json& node : entities) {
        const SceneEntityReferences references;
        ArchiveState state{.loading = true, .nodes = {&node}, .entities = &references};
        Archive archive(state);

        std::string name;
        archive.Field("name", name);
        std::uint64_t id = 0;
        archive.Field("id", id);
        std::optional<EntityId> wanted;
        if (options.ids) {
            if (const auto found = options.ids->find(static_cast<EntityId>(id)); found != options.ids->end()) {
                wanted = found->second;
            }
        }
        if (!wanted && options.keepIds) {
            wanted = static_cast<EntityId>(id);
        }
        Entity& entity =
            wanted ? scene.CreateEntityWithId(*wanted, std::move(name)) : scene.CreateEntity(std::move(name));
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
        int layer = 0;
        archive.Field("layer", layer);
        entity.m_layer = static_cast<std::uint8_t>(std::clamp(layer, 0, Entity::LayerCount - 1));

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

    // Instances whose root was saved along keep their links; their other entities saved alone become ordinary.
    std::unordered_map<std::uint64_t, Entity*> prefabRoots;
    for (std::size_t i = 0; i < created.size(); ++i) {
        const auto asset = entities[i].find("prefabAsset");
        const auto id = entities[i].find("id");
        if (asset == entities[i].end() || !asset->is_string() || id == entities[i].end() || !id->is_number_unsigned()) {
            continue;
        }
        const auto expanded = prefabAssets.find(id->get<std::uint64_t>());
        AssetRef<Prefab> prefab = expanded != prefabAssets.end() ? expanded->second : AssetRef<Prefab>();
        if (!prefab) {
            const auto assetId = AssetId::Parse(asset->get<std::string>());
            prefab = assetId ? managed(*assetId) : AssetRef<Prefab>();
        }
        if (!prefab) {
            Log::GetEngineLogger().Warning("Entity '{}' loads unlinked from its prefab {}, which is unavailable",
                                           created[i]->m_name, asset->get<std::string>());
            continue;
        }
        created[i]->m_prefabAsset = std::move(prefab);
        prefabRoots[id->get<std::uint64_t>()] = created[i];
    }
    std::unordered_map<std::uint64_t, IdMap> localIds;
    for (std::size_t i = 0; i < created.size(); ++i) {
        const auto root = entities[i].find("prefabRoot");
        const auto local = entities[i].find("prefabLocal");
        if (root == entities[i].end() || !root->is_number_unsigned() || local == entities[i].end() ||
            !local->is_number_unsigned() || local->get<std::uint64_t>() == 0) {
            continue;
        }
        const auto found = prefabRoots.find(root->get<std::uint64_t>());
        if (found != prefabRoots.end()) {
            created[i]->m_prefabRoot = found->second->m_id;
            created[i]->m_prefabLocalId = local->get<std::uint64_t>();
            localIds[root->get<std::uint64_t>()][local->get<std::uint64_t>()] = created[i]->m_id;
        }
    }
    // Plain references of entities expanded from prefabs are ids of their prefab, which their scope maps.
    std::vector<std::optional<IdMap>> scopeIds(scopes.size());
    const auto referenceIds = [&](const json& node) -> const IdMap& {
        const auto scope = node.find("referenceScope");
        if (scope == node.end() || !scope->is_number_unsigned() || scope->get<std::size_t>() >= scopes.size() ||
            !scopes[scope->get<std::size_t>()].is_object()) {
            return ids;
        }
        auto& mapped = scopeIds[scope->get<std::size_t>()];
        if (!mapped) {
            mapped.emplace();
            for (const auto& [key, saved] : scopes[scope->get<std::size_t>()].items()) {
                const auto local = ParsePrefabLocalId(key);
                const auto found = saved.is_number_unsigned() ? ids.find(saved.get<std::uint64_t>()) : ids.end();
                if (local) {
                    (*mapped)[*local] = found != ids.end() ? found->second : EntityId::Invalid;
                }
            }
        }
        return *mapped;
    };

    const Scene* referenced = options.keepExternalReferences ? &scene : nullptr;
    for (std::size_t i = 0; i < created.size(); ++i) {
        Entity& entity = *created[i];
        // Parents only come from the saved entities, so a saved subtree loads as a new one wherever it came from.
        const SceneEntityReferences parentReferences(&ids);
        ArchiveState parentState{.loading = true, .nodes = {&entities[i]}, .entities = &parentReferences};
        Archive parentArchive(parentState);
        EntityId parentId = EntityId::Invalid;
        parentArchive.Field("parent", parentId);
        Entity* parent = scene.GetEntity(parentId);
        if (parent && parent != &entity && !parent->IsDescendantOf(entity)) {
            entity.SetParent(parent);
        }

        const auto components = entities[i].find("components");
        if (components != entities[i].end() && components->is_array()) {
            const auto root = entities[i].find("prefabRoot");
            const auto instance = entity.m_prefabRoot != EntityId::Invalid && root != entities[i].end()
                                      ? localIds.find(root->get<std::uint64_t>())
                                      : localIds.end();
            const IdMap* local = instance != localIds.end() ? &instance->second : nullptr;
            const IdMap& raw = referenceIds(entities[i]);
            for (json& saved : *components) {
                LoadComponent(entity, saved, std::nullopt, raw, referenced, manager, local, &ids,
                              options.keepUnknownComponents);
            }
        }
    }

    for (std::size_t i = 0; i < created.size(); ++i) {
        const auto sibling = entities[i].find("siblingIndex");
        if (sibling != entities[i].end() && sibling->is_number_unsigned()) {
            created[i]->SetSiblingIndex(sibling->get<std::size_t>());
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

Component* SceneSerializer::LoadComponent(Entity& entity, json& saved, std::optional<std::size_t> index,
                                          const IdMap& ids, const Scene* referenced, AssetManager* assets,
                                          const IdMap* localIds, const IdMap* externalIds, bool keepUnknown) {
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
        ReportUnregistered(entity.m_name, type, keepUnknown);
        if (!keepUnknown) {
            return nullptr;
        }
        owned = std::make_unique<MissingComponent>(type, hasData ? data->dump() : "{}");
    }
    Component& component = *owned;
    component.m_id = AssetId::Parse(GetString(saved, "id")).value_or(component.m_id);
    component.m_enabled = GetBool(saved, "enabled", true);
    entity.Attach(std::move(owned), index);

    if (hasData && !dynamic_cast<MissingComponent*>(&component)) {
        const SceneEntityReferences references(&ids, referenced, nullptr, localIds, externalIds);
        ArchiveState state{.loading = true,
                           .nodes = {&*data},
                           .context = assets,
                           .contextType = &typeid(AssetManager),
                           .entities = &references};
        Archive archive(state);
        component.Serialize(archive);
        Validate(component);
    }
    return &component;
}

bool SceneSerializer::ReplaceComponent(Component& component, std::string_view text, AssetManager* assets) {
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
        const SceneEntityReferences references;
        ArchiveState state{.loading = true,
                           .nodes = {&*data},
                           .context = assets,
                           .contextType = &typeid(AssetManager),
                           .entities = &references};
        Archive archive(state);
        component.Serialize(archive);
        Validate(component);
    }
    component.SetEnabled(GetBool(saved, "enabled", component.m_enabled));
    return true;
}

} // namespace Detail

std::string Scene::SaveToString(bool expandPrefabs) const {
    json document = MakeDocument();
    Detail::SceneSerializer::PrefabRoots prefabs;
    if (!expandPrefabs) {
        for (const Entity* root : m_roots) {
            Detail::SceneSerializer::CollectPrefabRoots(*root, prefabs);
        }
    }
    std::unordered_map<EntityId, Detail::SceneSerializer::IdMap> localIds;
    for (const Entity* root : m_roots) {
        if (!root->m_destroyed) {
            Detail::SceneSerializer::SaveTree(*root, document["entities"], prefabs, localIds);
        }
    }
    return Dump(document);
}

std::string Scene::SaveEntitiesToString(std::span<Entity* const> entities, bool expandPrefabs) const {
    json document = MakeDocument();
    Detail::SceneSerializer::PrefabRoots prefabs;
    if (!expandPrefabs) {
        for (const Entity* entity : entities) {
            Detail::SceneSerializer::CollectPrefabRoots(*entity, prefabs);
        }
    }
    std::unordered_map<EntityId, Detail::SceneSerializer::IdMap> localIds;
    for (const Entity* entity : entities) {
        SPOT_ASSERT(&entity->GetScene() == this, "The entity belongs to another scene");
        const bool savedWithAncestor = std::ranges::any_of(
            entities, [entity](const Entity* other) { return other != entity && entity->IsDescendantOf(*other); });
        if (!entity->m_destroyed && !savedWithAncestor) {
            Detail::SceneSerializer::SaveTree(*entity, document["entities"], prefabs, localIds);
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
    // The assets the scene names, and those they refer to, are read together before its entities need them one by
    // one, and kept until it is loaded. Only whole scenes do, as instantiating prefabs often, such as projectiles,
    // finds their assets loaded already.
    AssetManager* manager = assets ? assets : Assets::GetManager();
    const auto preloaded = manager ? manager->PreloadReferencedBy(text) : std::vector<std::shared_ptr<const void>>();
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

std::string Entity::SaveToString(bool expandPrefabs) const {
    SPOT_ASSERT(!m_destroyed, "A destroyed entity cannot be saved");
    json document = MakeDocument();
    Detail::SceneSerializer::PrefabRoots prefabs;
    if (!expandPrefabs) {
        Detail::SceneSerializer::CollectPrefabRoots(*this, prefabs);
    }
    std::unordered_map<EntityId, Detail::SceneSerializer::IdMap> localIds;
    Detail::SceneSerializer::SaveTree(*this, document["entities"], prefabs, localIds);
    return Dump(document);
}

namespace {

// Goes through a component's fields twice: first it clears its references to the assets, then it puts back the ids
// they had, which makes the archive load them again.
class AssetReloadBackend final : public ArchiveBackend {
public:
    AssetReloadBackend(std::span<const AssetId> assets, std::vector<AssetId>& ids, bool restoring,
                       AssetManager* manager)
        : m_assets(assets), m_ids(ids), m_restoring(restoring), m_manager(manager) {}

    bool IsLoading() const override { return true; }

    void Field(std::string_view, bool&, const FieldOptions&) override {}
    void Field(std::string_view, int&, const FieldOptions&) override {}
    void Field(std::string_view, std::uint32_t&, const FieldOptions&) override {}
    void Field(std::string_view, std::int64_t&, const FieldOptions&) override {}
    void Field(std::string_view, std::uint64_t&, const FieldOptions&) override {}
    void Field(std::string_view, float&, const FieldOptions&) override {}
    void Field(std::string_view, double&, const FieldOptions&) override {}
    void Field(std::string_view, std::string&, const FieldOptions&) override {}
    void Field(std::string_view, Vector2&, const FieldOptions&) override {}
    void Field(std::string_view, Vector3&, const FieldOptions&) override {}
    void Field(std::string_view, Vector4&, const FieldOptions&) override {}
    void Field(std::string_view, Quaternion&, const FieldOptions&) override {}
    void Field(std::string_view, Color&, const FieldOptions&) override {}
    void Field(std::string_view, EntityId&, const FieldOptions&) override {}

    void Field(std::string_view, AssetId& value, std::optional<AssetType>, const FieldOptions&) override {
        if (m_restoring) {
            value = m_next < m_ids.size() ? m_ids[m_next++] : value;
            return;
        }
        m_ids.push_back(value);
        if (std::ranges::find(m_assets, value) != m_assets.end()) {
            value = {};
        }
    }

    bool BeginObject(std::string_view, const FieldOptions&) override { return true; }
    void EndObject() override {}
    bool BeginArray(std::string_view, std::size_t&, const FieldOptions&) override { return true; }
    void BeginElement(std::size_t) override {}
    void EndElement() override {}
    void EndArray() override {}
    void* GetContext(const std::type_info& type) const override {
        return type == typeid(AssetManager) ? m_manager : nullptr;
    }

private:
    std::span<const AssetId> m_assets;
    std::vector<AssetId>& m_ids;
    bool m_restoring;
    AssetManager* m_manager;
    std::size_t m_next = 0;
};

} // namespace

void ReloadAssetReferences(Component& component, std::span<const AssetId> assets, AssetManager* manager) {
    if (assets.empty()) {
        return;
    }
    std::vector<AssetId> ids;
    AssetReloadBackend clearing(assets, ids, false, manager);
    Archive clear(clearing);
    Detail::SceneSerializer::Serialize(component, clear);
    if (std::ranges::none_of(ids, [&](AssetId id) { return std::ranges::find(assets, id) != assets.end(); })) {
        return;
    }
    AssetReloadBackend restoring(assets, ids, true, manager);
    Archive restore(restoring);
    Detail::SceneSerializer::Serialize(component, restore);
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
