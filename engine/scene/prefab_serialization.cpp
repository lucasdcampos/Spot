#include "scene/scene_serializer.h"

#include "assets/assets.h"
#include "core/log.h"
#include "scene/prefab.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <unordered_set>

namespace Spot::Detail {

namespace {

using nlohmann::json;

std::string Escape(std::string_view key) {
    std::string result;
    for (const char c : key) {
        result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    }
    return result;
}

std::optional<std::uint64_t> ParseLocalId(std::string_view text) {
    std::uint64_t id = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
    return error == std::errc() && end == text.data() + text.size() && id != 0 ? std::optional(id) : std::nullopt;
}

// Arrays are values, so inserting one item does not override unrelated indices after the source changes.
void Diff(const json& before, const json& after, std::string_view path, json& patches) {
    if (before == after) {
        return;
    }
    if (!before.is_object() || !after.is_object()) {
        patches.push_back({{"op", "replace"}, {"path", path}, {"value", after}});
        return;
    }
    for (const auto& [key, value] : before.items()) {
        const std::string child = std::string(path) + '/' + Escape(key);
        if (!after.contains(key)) {
            patches.push_back({{"op", "remove"}, {"path", child}});
        } else {
            Diff(value, after[key], child, patches);
        }
    }
    for (const auto& [key, value] : after.items()) {
        if (!before.contains(key)) {
            patches.push_back({{"op", "add"}, {"path", std::string(path) + '/' + Escape(key)}, {"value", value}});
        }
    }
}

bool HasComponentTarget(const json& entity, const json& patch) {
    if (!patch.is_object() || !patch.contains("path") || !patch["path"].is_string()) {
        return false;
    }
    const std::string path = patch["path"].get<std::string>();
    constexpr std::string_view Prefix = "/components/";
    if (!path.starts_with(Prefix)) {
        return true;
    }
    const std::size_t end = path.find('/', Prefix.size());
    const std::string id = path.substr(Prefix.size(), end - Prefix.size());
    return entity["components"].contains(id) || (end == std::string::npos && patch.value("op", "") == "add");
}

bool Apply(json& entity, const json& patches, AssetId prefab, std::uint64_t local) {
    if (!patches.is_array()) {
        return false;
    }
    for (const json& patch : patches) {
        if (!patch.is_object() || !patch.contains("op") || !patch["op"].is_string() || !patch.contains("path") ||
            !patch["path"].is_string()) {
            return false;
        }
        const std::string op = patch["op"].get<std::string>();
        const std::string path = patch["path"].get<std::string>();
        if ((op != "add" && op != "replace" && op != "remove") || path.empty() || path.front() != '/' ||
            ((op == "add" || op == "replace") && !patch.contains("value"))) {
            return false;
        }
        if (!HasComponentTarget(entity, patch)) {
            Log::GetEngineLogger().Warning("Prefab {} entity {} no longer has override target '{}'", prefab.ToString(),
                                           local, path);
            continue;
        }
        try {
            const json::json_pointer pointer(path);
            if (op == "remove") {
                if (entity.contains(pointer)) {
                    entity = entity.patch(json::array({patch}));
                }
            } else {
                // A field added to game code need not have existed in an older source document.
                entity[pointer] = patch["value"];
            }
        } catch (const json::exception&) {
            Log::GetEngineLogger().Warning("Prefab {} entity {} could not apply override '{}'", prefab.ToString(),
                                           local, path);
        }
    }
    return true;
}

void FlattenComponents(json& node) {
    json components = json::array();
    std::unordered_set<std::string> placed;
    const auto order = node.find("componentOrder");
    if (order != node.end() && order->is_array()) {
        for (const json& entry : *order) {
            if (entry.is_string()) {
                const auto id = entry.get<std::string>();
                if (node["components"].contains(id) && placed.insert(id).second) {
                    components.push_back(node["components"][id]);
                }
            }
        }
    }
    for (const auto& [id, component] : node["components"].items()) {
        if (placed.insert(id).second) {
            components.push_back(component);
        }
    }
    node["components"] = std::move(components);
    node.erase("componentOrder");
}

} // namespace

void SceneSerializer::CollectPrefabRoots(const Entity& entity, PrefabRoots& roots) {
    if (entity.m_destroyed) {
        return;
    }
    if (entity.m_prefabInstance && entity.m_prefabInstance->asset.GetId().IsValid()) {
        roots.insert(entity.m_id);
    }
    for (const Entity* child : entity.m_children) {
        CollectPrefabRoots(*child, roots);
    }
}

json SceneSerializer::Snapshot(const Entity& entity, const IdMap& prefabIds, bool root) {
    json node;
    SaveEntity(entity, node, &prefabIds);
    node.erase("id");
    if (root) {
        node.erase("parent");
    } else {
        node["siblingIndex"] = entity.GetSiblingIndex();
    }
    json components = json::object();
    json order = json::array();
    for (const json& component : node["components"]) {
        const auto id = component["id"].get<std::string>();
        components[id] = component;
        order.push_back(id);
    }
    node["components"] = std::move(components);
    node["componentOrder"] = std::move(order);
    return node;
}

Entity* SceneSerializer::InstantiatePrefab(Scene& scene, const AssetRef<Prefab>& prefab,
                                           const InstantiateOptions& options, bool validate) {
    InstantiateOptions placement = options;
    placement.keepIds = false;
    placement.keepExternalReferences = false;
    IdMap ids;
    const auto roots = Load(scene, prefab->GetEntityText(), placement, &ids, validate);
    if (!roots || roots->empty()) {
        return nullptr;
    }
    Entity& root = *roots->front();
    if (!prefab.GetId().IsValid()) {
        return &root;
    }
    auto state = std::make_shared<PrefabInstanceState>();
    state->asset = prefab;
    state->entities = ids;
    IdMap localIds;
    for (const auto& [local, actual] : ids) {
        localIds[static_cast<std::uint64_t>(actual)] = static_cast<EntityId>(local);
    }
    for (const auto& [local, actual] : ids) {
        Entity* entity = scene.GetEntity(actual);
        if (!entity) {
            continue;
        }
        entity->m_prefabRoot = root.m_id;
        entity->m_prefabLocalId = local;
        state->baseline[std::to_string(local)] = Snapshot(*entity, localIds, entity == &root);
    }
    root.m_prefabInstance = std::move(state);
    return &root;
}

json SceneSerializer::SavePrefab(const Entity& root) {
    const auto& instance = *root.m_prefabInstance;
    json saved{{"id", static_cast<std::uint64_t>(root.m_id)},
               {"prefab", instance.asset.GetId().ToString()},
               {"entityIds", json::object()},
               {"overrides", json::object()},
               {"removedEntities", json::array()}};
    if (root.m_parent) {
        saved["parent"] = static_cast<std::uint64_t>(root.m_parent->m_id);
    }
    IdMap localIds;
    for (const auto& [local, actual] : instance.entities) {
        if (actual == EntityId::Invalid) {
            continue;
        }
        saved["entityIds"][std::to_string(local)] = static_cast<std::uint64_t>(actual);
        localIds[static_cast<std::uint64_t>(actual)] = static_cast<EntityId>(local);
    }
    std::unordered_set<std::uint64_t> removed = instance.removedEntities;
    for (const auto& [key, before] : instance.baseline.items()) {
        const auto local = *ParseLocalId(key);
        const auto found = instance.entities.find(local);
        const Entity* entity = found != instance.entities.end() ? root.GetScene().GetEntity(found->second) : nullptr;
        if (!entity || entity->m_prefabRoot != root.m_id) {
            removed.insert(local);
            continue;
        }
        const json after = Snapshot(*entity, localIds, entity == &root);
        json patches = json::array();
        Diff(before, after, "", patches);
        if (const auto retained = instance.retainedOverrides.find(key); retained != instance.retainedOverrides.end()) {
            for (const json& patch : *retained) {
                if (!HasComponentTarget(before, patch)) {
                    patches.push_back(patch);
                }
            }
        }
        if (!patches.empty()) {
            saved["overrides"][key] = std::move(patches);
        }
    }
    for (const auto& [local, patches] : instance.retainedOverrides.items()) {
        if (!instance.baseline.contains(local)) {
            saved["overrides"][local] = patches;
        }
    }
    std::vector<std::uint64_t> sorted(removed.begin(), removed.end());
    std::ranges::sort(sorted);
    saved["removedEntities"] = sorted;
    return saved;
}

bool SceneSerializer::ExpandPrefabs(json& document, AssetManager* assets, std::vector<PrefabLoadGroup>& groups) {
    assets = assets ? assets : Assets::GetManager();
    json expanded = json::array();
    std::unordered_set<std::uint64_t> usedIds;
    for (const json& node : document["entities"]) {
        if (node.contains("id") && node["id"].is_number_unsigned()) {
            usedIds.insert(node["id"].get<std::uint64_t>());
        }
        if (node.contains("prefab") && node.contains("entityIds") && node["entityIds"].is_object()) {
            for (const json& id : node["entityIds"]) {
                if (id.is_number_unsigned()) {
                    usedIds.insert(id.get<std::uint64_t>());
                }
            }
        }
    }
    std::uint64_t nextId = 1;
    const auto allocate = [&] {
        while (usedIds.contains(nextId)) {
            ++nextId;
        }
        usedIds.insert(nextId);
        return nextId++;
    };
    for (const json& record : document["entities"]) {
        if (!record.contains("prefab")) {
            expanded.push_back(record);
            continue;
        }
        if (!record["prefab"].is_string() || !record.contains("id") || !record["id"].is_number_unsigned() ||
            record["id"] == 0 || !record.contains("entityIds") || !record["entityIds"].is_object() ||
            !record.contains("overrides") || !record["overrides"].is_object() || !record.contains("removedEntities") ||
            !record["removedEntities"].is_array()) {
            Log::GetEngineLogger().Warning("Could not load the scene: a prefab instance has invalid metadata");
            return false;
        }
        const auto assetId = AssetId::Parse(record["prefab"].get<std::string>());
        const auto prefab = assetId && assets ? assets->Load<Prefab>(*assetId) : AssetRef<Prefab>();
        if (!prefab) {
            Log::GetEngineLogger().Error("Could not load the scene: prefab '{}' is unavailable",
                                         record["prefab"].get<std::string>());
            return false;
        }
        Scene scratch;
        Entity* root = InstantiatePrefab(scratch, prefab, {.assets = assets}, false);
        if (!root || !root->m_prefabInstance) {
            return false;
        }
        auto state = root->m_prefabInstance;
        PrefabLoadGroup group{.instance = state, .root = record["id"].get<std::uint64_t>(), .savedIds = {}};
        std::unordered_set<std::uint64_t> distinct;
        for (const auto& [key, id] : record["entityIds"].items()) {
            const auto local = ParseLocalId(key);
            if (!local || !id.is_number_unsigned() || id == 0 || !distinct.insert(id.get<std::uint64_t>()).second) {
                return false;
            }
            group.savedIds[*local] = static_cast<EntityId>(id.get<std::uint64_t>());
        }
        const auto localRoot = root->m_prefabLocalId;
        group.savedIds[localRoot] = static_cast<EntityId>(group.root);
        for (const auto& [local, unused] : state->entities) {
            (void)unused;
            if (!group.savedIds.contains(local)) {
                group.savedIds[local] = static_cast<EntityId>(allocate());
            }
        }
        for (const json& removed : record["removedEntities"]) {
            if (!removed.is_number_unsigned() || removed == 0 || removed == localRoot) {
                return false;
            }
            state->removedEntities.insert(removed.get<std::uint64_t>());
        }
        state->retainedOverrides = record["overrides"];
        json nodes = state->baseline;
        for (const auto& [key, patches] : record["overrides"].items()) {
            const auto local = ParseLocalId(key);
            if (!local || !patches.is_array()) {
                return false;
            }
            if (!nodes.contains(key)) {
                Log::GetEngineLogger().Warning("Prefab {} no longer has overridden entity {}",
                                               prefab.GetId().ToString(), *local);
            } else if (!Apply(nodes[key], patches, prefab.GetId(), *local)) {
                return false;
            }
        }
        // Removing an inherited parent removes its source subtree, including children added by later source edits.
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& [key, node] : nodes.items()) {
                const auto parent = node.find("parent");
                if (parent != node.end() && parent->is_object() && parent->contains("prefabEntity") &&
                    (*parent)["prefabEntity"].is_number_unsigned() &&
                    state->removedEntities.contains((*parent)["prefabEntity"].get<std::uint64_t>())) {
                    changed = state->removedEntities.insert(*ParseLocalId(key)).second || changed;
                }
            }
        }
        const auto source = json::parse(prefab->GetEntityText());
        for (const json& sourceNode : source["entities"]) {
            const auto local = sourceNode["id"].get<std::uint64_t>();
            if (state->removedEntities.contains(local)) {
                continue;
            }
            json node = nodes[std::to_string(local)];
            node["id"] = static_cast<std::uint64_t>(group.savedIds.at(local));
            const auto parent = node.find("parent");
            if (local == localRoot) {
                if (record.contains("parent")) {
                    node["parent"] = record["parent"];
                }
            } else if (parent != node.end() && parent->is_object()) {
                if (parent->contains("prefabEntity") && (*parent)["prefabEntity"].is_number_unsigned()) {
                    const auto found = group.savedIds.find((*parent)["prefabEntity"].get<std::uint64_t>());
                    node["parent"] = found != group.savedIds.end() ? static_cast<std::uint64_t>(found->second) : 0;
                } else if (parent->contains("sceneEntity")) {
                    node["parent"] = (*parent)["sceneEntity"];
                }
            }
            if (!node["components"].is_object()) {
                return false;
            }
            FlattenComponents(node);
            expanded.push_back(std::move(node));
        }
        groups.push_back(std::move(group));
    }
    if (!groups.empty()) {
        std::unordered_set<std::uint64_t> ids;
        for (const json& node : expanded) {
            if (node.contains("id") && node["id"].is_number_unsigned() && node["id"] != 0 &&
                !ids.insert(node["id"].get<std::uint64_t>()).second) {
                Log::GetEngineLogger().Warning("Could not load the scene: prefab and scene entities share a saved id");
                return false;
            }
        }
    }
    document["entities"] = std::move(expanded);
    return true;
}

void SceneSerializer::RestorePrefabGroups(Scene& scene, std::vector<PrefabLoadGroup>& groups, const IdMap& ids) {
    for (auto& group : groups) {
        const auto rootId = ids.at(group.root);
        Entity* root = scene.GetEntity(rootId);
        auto& state = *group.instance;
        state.entities.clear();
        for (const auto& [local, saved] : group.savedIds) {
            const auto actual = state.baseline.contains(std::to_string(local)) && !state.removedEntities.contains(local)
                                    ? ids.find(static_cast<std::uint64_t>(saved))
                                    : ids.end();
            state.entities[local] = actual != ids.end() ? actual->second : EntityId::Invalid;
            if (actual != ids.end()) {
                Entity* entity = scene.GetEntity(actual->second);
                entity->m_prefabRoot = rootId;
                entity->m_prefabLocalId = local;
            }
        }
        root->m_prefabInstance = group.instance;
    }
}

} // namespace Spot::Detail
