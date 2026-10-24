#include "spot/scene/scene_serializer.h"

#include "spot/assets/assets.h"
#include "spot/core/debug/log.h"
#include "spot/scene/prefab.h"
#include "spot/scene/prefab_document.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace Spot::Detail {

namespace {

using nlohmann::json;

thread_local std::vector<AssetId> s_flatteningPrefabs;

class FlattenScope {
public:
    explicit FlattenScope(AssetId asset) { s_flatteningPrefabs.push_back(asset); }
    ~FlattenScope() { s_flatteningPrefabs.pop_back(); }

    FlattenScope(const FlattenScope&) = delete;
    FlattenScope& operator=(const FlattenScope&) = delete;
};

std::uint64_t NestedEntityId(std::uint64_t root, std::uint64_t local) {
    // Fixed integer mixing keeps new inherited identities stable when other source children are inserted.
    auto mix = [](std::uint64_t value) {
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    };
    return (mix(root ^ mix(local)) & ((1ULL << 62) - 1)) | (1ULL << 62);
}

bool IsRecordValid(const json& record) {
    return record["prefab"].is_string() && record.contains("id") && record["id"].is_number_unsigned() &&
           record["id"] != 0 && record.contains("entityIds") && record["entityIds"].is_object() &&
           record.contains("overrides") && record["overrides"].is_object() && record.contains("removedEntities") &&
           record["removedEntities"].is_array();
}

// A flat prefab, flattening it first when it is not, as prefabs made in memory may not be.
AssetRef<Prefab> GetFlat(const AssetRef<Prefab>& prefab, const Prefab::NestedPrefabs& prefabs) {
    if (prefab->IsFlat()) {
        return prefab;
    }
    if (s_flatteningPrefabs.size() >= 64 ||
        std::ranges::find(s_flatteningPrefabs, prefab.GetId()) != s_flatteningPrefabs.end()) {
        Log::GetEngineLogger().Warning("Could not expand prefab {}: cyclic dependency or nesting deeper than 64",
                                       prefab.GetId().ToString());
        return {};
    }
    const FlattenScope flattening(prefab.GetId());
    auto flat = prefab->Flatten(prefabs);
    return flat ? AssetRef<Prefab>(prefab.GetId(), std::make_shared<const Prefab>(std::move(*flat)))
                : AssetRef<Prefab>();
}

} // namespace

void SceneSerializer::CollectPrefabRoots(const Entity& entity, PrefabRoots& roots) {
    if (entity.m_destroyed) {
        return;
    }
    if (entity.m_prefabRoot == entity.m_id && entity.m_prefabAsset.GetId().IsValid()) {
        roots.insert(entity.m_id);
    }
    for (const Entity* child : entity.m_children) {
        CollectPrefabRoots(*child, roots);
    }
}

bool SceneSerializer::ExpandPrefabs(json& document, const Prefab::NestedPrefabs& prefabs, PrefabAssets* assets) {
    json& entities = document["entities"];
    if (std::ranges::none_of(entities, [](const json& node) { return node.contains("prefab"); })) {
        return true;
    }
    json scopes = document.contains("referenceScopes") && document["referenceScopes"].is_array()
                      ? document["referenceScopes"]
                      : json::array();
    json expanded = json::array();
    std::unordered_set<std::uint64_t> usedIds;
    for (const json& node : entities) {
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
    for (const json& record : entities) {
        if (!record.contains("prefab")) {
            expanded.push_back(record);
            continue;
        }
        if (!IsRecordValid(record)) {
            Log::GetEngineLogger().Warning("Could not load the scene: a prefab instance has invalid metadata");
            return false;
        }
        const auto assetId = AssetId::Parse(record["prefab"].get<std::string>());
        AssetRef<Prefab> prefab = assetId && prefabs ? prefabs(*assetId) : AssetRef<Prefab>();
        if (!prefab) {
            Log::GetEngineLogger().Error("Could not load the scene: prefab '{}' is unavailable",
                                         record["prefab"].get<std::string>());
            return false;
        }
        prefab = GetFlat(prefab, prefabs);
        if (!prefab) {
            return false;
        }
        const json& source = prefab->GetDocument().document;
        const json& sourceEntities = source["entities"];
        const auto sourceScopes = source.find("referenceScopes");
        const auto rootId = record["id"].get<std::uint64_t>();

        std::uint64_t localRoot = 0;
        for (const json& node : sourceEntities) {
            if (!node.contains("parent")) {
                localRoot = node["id"].get<std::uint64_t>();
            }
        }
        std::unordered_map<std::uint64_t, std::uint64_t> savedIds;
        std::unordered_set<std::uint64_t> distinct;
        for (const auto& [key, id] : record["entityIds"].items()) {
            const auto local = ParsePrefabLocalId(key);
            if (!local || !id.is_number_unsigned() || id == 0 || !distinct.insert(id.get<std::uint64_t>()).second ||
                !savedIds.try_emplace(*local, id.get<std::uint64_t>()).second) {
                Log::GetEngineLogger().Warning("Could not load the scene: a prefab instance has invalid entity ids");
                return false;
            }
        }
        savedIds[localRoot] = rootId;
        for (const json& node : sourceEntities) {
            const auto local = node["id"].get<std::uint64_t>();
            if (!savedIds.contains(local)) {
                const auto saved = NestedEntityId(rootId, local);
                if (!usedIds.insert(saved).second) {
                    Log::GetEngineLogger().Warning("Could not load prefab {}: inherited entity ids collide",
                                                   prefab.GetId().ToString());
                    return false;
                }
                savedIds[local] = saved;
            }
        }
        const auto savedId = [&](std::uint64_t local) {
            const auto found = savedIds.find(local);
            return found != savedIds.end() ? found->second : std::uint64_t{0};
        };

        json nodes = json::object();
        std::vector<std::uint64_t> order;
        std::unordered_map<std::uint64_t, std::size_t> siblingCounts;
        std::unordered_map<std::uint64_t, std::size_t> sourceScopeOf;
        for (const json& saved : sourceEntities) {
            const auto local = saved["id"].get<std::uint64_t>();
            json node = KeyPrefabEntity(saved);
            if (const auto parent = saved.find("parent"); parent != saved.end() && parent->is_number_unsigned()) {
                node["parent"] = {{"prefabEntity", parent->get<std::uint64_t>()}};
                node["siblingIndex"] = siblingCounts[parent->get<std::uint64_t>()]++;
            }
            // The prefab's own references to its entities, from instances flattened into it.
            ForEachEntityMarker(node["components"], [&](json& marker) {
                if (marker.contains("sceneEntity") && marker["sceneEntity"].is_number_unsigned()) {
                    marker = {{"sceneEntity", savedId(marker["sceneEntity"].get<std::uint64_t>())}};
                }
            });
            if (const auto scope = saved.find("referenceScope"); scope != saved.end() && scope->is_number_unsigned()) {
                sourceScopeOf[local] = scope->get<std::size_t>();
            }
            nodes[std::to_string(local)] = std::move(node);
            order.push_back(local);
        }

        std::unordered_set<std::uint64_t> removed;
        for (const json& entity : record["removedEntities"]) {
            if (!entity.is_number_unsigned() || entity == 0 || entity == localRoot) {
                Log::GetEngineLogger().Warning("Could not load the scene: a prefab instance removes an invalid entity");
                return false;
            }
            removed.insert(entity.get<std::uint64_t>());
        }
        for (const auto& [key, patches] : record["overrides"].items()) {
            const auto local = ParsePrefabLocalId(key);
            if (!local || !patches.is_array()) {
                Log::GetEngineLogger().Warning("Could not load the scene: a prefab instance has invalid overrides");
                return false;
            }
            if (!nodes.contains(key)) {
                Log::GetEngineLogger().Warning("Prefab {} no longer has overridden entity {}",
                                               prefab.GetId().ToString(), *local);
            } else if (!ApplyPrefabOverrides(nodes[key], patches, prefab.GetId(), *local)) {
                Log::GetEngineLogger().Warning("Could not load the scene: a prefab instance has invalid overrides");
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
                    removed.contains((*parent)["prefabEntity"].get<std::uint64_t>())) {
                    changed = removed.insert(*ParsePrefabLocalId(key)).second || changed;
                }
            }
        }

        json ownScope = json::object();
        for (const auto& [local, saved] : savedIds) {
            ownScope[std::to_string(local)] = saved;
        }
        const std::size_t ownScopeIndex = scopes.size();
        scopes.push_back(std::move(ownScope));
        std::unordered_map<std::size_t, std::size_t> composedScopes;
        for (const auto local : order) {
            if (removed.contains(local)) {
                continue;
            }
            json node = std::move(nodes[std::to_string(local)]);
            if (local == localRoot) {
                node.erase("parent");
                node.erase("siblingIndex");
                if (record.contains("parent")) {
                    node["parent"] = record["parent"];
                }
                if (record.contains("siblingIndex")) {
                    node["siblingIndex"] = record["siblingIndex"];
                }
                node["prefabAsset"] = prefab.GetId().ToString();
            } else if (const auto parent = node.find("parent"); parent != node.end() && parent->is_object()) {
                const auto inside = parent->find("prefabEntity");
                const auto outside = parent->find("sceneEntity");
                if (inside != parent->end() && inside->is_number_unsigned()) {
                    node["parent"] = savedId(inside->get<std::uint64_t>());
                } else if (outside != parent->end()) {
                    node["parent"] = *outside;
                } else {
                    node.erase("parent");
                }
            }
            ForEachEntityMarker(node, [&](json& marker) {
                if (marker.contains("prefabEntity") && marker["prefabEntity"].is_number_unsigned()) {
                    marker = {{"sceneEntity", savedId(marker["prefabEntity"].get<std::uint64_t>())}};
                }
            });
            node["id"] = savedId(local);
            node["prefabRoot"] = rootId;
            node["prefabLocal"] = local;
            const auto sourceScope = sourceScopeOf.find(local);
            if (sourceScope == sourceScopeOf.end() || sourceScopes == source.end() ||
                sourceScope->second >= sourceScopes->size()) {
                node["referenceScope"] = ownScopeIndex;
            } else {
                auto [composed, added] = composedScopes.try_emplace(sourceScope->second, scopes.size());
                if (added) {
                    json scope = json::object();
                    for (const auto& [key, value] : (*sourceScopes)[sourceScope->second].items()) {
                        scope[key] = value.is_number_unsigned() ? savedId(value.get<std::uint64_t>()) : 0;
                    }
                    scopes.push_back(std::move(scope));
                }
                node["referenceScope"] = composed->second;
            }
            if (!node["components"].is_object()) {
                return false;
            }
            UnkeyPrefabEntity(node);
            expanded.push_back(std::move(node));
        }
        if (assets) {
            (*assets)[rootId] = prefab;
        }
    }
    std::unordered_set<std::uint64_t> ids;
    for (const json& node : expanded) {
        if (node.contains("id") && node["id"].is_number_unsigned() && node["id"] != 0 &&
            !ids.insert(node["id"].get<std::uint64_t>()).second) {
            Log::GetEngineLogger().Warning("Could not load the scene: prefab and scene entities share a saved id");
            return false;
        }
    }
    entities = std::move(expanded);
    document["referenceScopes"] = std::move(scopes);
    return true;
}

Entity* SceneSerializer::InstantiatePrefab(Scene& scene, const AssetRef<Prefab>& prefab,
                                           const InstantiateOptions& options) {
    InstantiateOptions placement = options;
    placement.keepIds = false;
    placement.keepExternalReferences = false;
    placement.ids = nullptr;
    std::optional<std::vector<Entity*>> roots;
    if (!prefab.GetId().IsValid()) {
        roots = Load(scene, prefab->GetDocument().document, placement);
    } else {
        AssetManager* assets = options.assets ? options.assets : Assets::GetManager();
        const Prefab::NestedPrefabs prefabs = [&](AssetId id) {
            return id == prefab.GetId() ? prefab : assets ? assets->Load<Prefab>(id) : AssetRef<Prefab>();
        };
        json document{{"format", "spot-scene"},
                      {"version", 2},
                      {"entities", json::array({{{"id", std::uint64_t{1}},
                                                 {"prefab", prefab.GetId().ToString()},
                                                 {"entityIds", json::object()},
                                                 {"overrides", json::object()},
                                                 {"removedEntities", json::array()}}})}};
        roots = Load(scene, std::move(document), placement, &prefabs);
    }
    return roots && !roots->empty() ? roots->front() : nullptr;
}

} // namespace Spot::Detail
