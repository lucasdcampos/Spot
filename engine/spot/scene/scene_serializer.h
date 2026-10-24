#pragma once

#include "spot/core/io/archive_state.h"
#include "spot/scene/prefab.h"
#include "spot/scene/scene.h"

#include <optional>
#include <unordered_set>

namespace Spot::Detail {

struct PrefabInstanceState {
    AssetRef<Prefab> asset;
    std::unordered_map<std::uint64_t, EntityId> entities;
    // Canonical, source-local fields, including defaults supplied by registered components.
    nlohmann::json baseline = nlohmann::json::object();
    nlohmann::json retainedOverrides = nlohmann::json::object();
    std::unordered_set<std::uint64_t> removedEntities;
};

struct PrefabLoadGroup {
    std::shared_ptr<PrefabInstanceState> instance;
    std::uint64_t root = 0;
    // Maps prefab-local ids to the document's saved ids before Load() remaps them to scene ids.
    std::unordered_map<std::uint64_t, EntityId> savedIds;
};

struct SceneSerializer {
    using IdMap = std::unordered_map<std::uint64_t, EntityId>;
    using PrefabRoots = std::unordered_set<EntityId>;

    static void Serialize(Component& component, Archive& archive);
    static void Validate(Component& component);
    static void CollectPrefabRoots(const Entity& entity, PrefabRoots& roots);
    static void SaveTree(const Entity& entity, nlohmann::json& entities, const PrefabRoots& roots);
    static void SaveEntity(const Entity& entity, nlohmann::json& node, const IdMap* prefabIds = nullptr);
    static std::optional<nlohmann::json> SaveComponent(const Component& component, const IdMap* prefabIds = nullptr);
    static std::optional<nlohmann::json> ParseDocument(std::string_view text);
    static std::optional<std::vector<Entity*>> Load(Scene& scene, std::string_view text,
                                                    const InstantiateOptions& options, IdMap* loadedIds = nullptr,
                                                    bool validateComponents = true, bool requireTree = false);
    static Component* LoadComponent(Entity& entity, nlohmann::json& saved, std::optional<std::size_t> index,
                                    const IdMap& ids, const Scene* referenced, AssetManager* assets,
                                    const IdMap* externalIds = nullptr, bool validate = true);
    static bool ReplaceComponent(Component& component, std::string_view text, AssetManager* assets);
    static Entity* InstantiatePrefab(Scene& scene, const AssetRef<Prefab>& prefab, const InstantiateOptions& options,
                                     bool validate = true);
    static nlohmann::json Snapshot(const Entity& entity, const IdMap& prefabIds, bool root);
    static nlohmann::json SavePrefab(const Entity& root);
    static std::vector<PrefabOverride> GetOverrides(const Entity& root);
    static std::optional<PrefabEdit> EditPrefab(const Entity& root, const PrefabOverrideScope& scope, bool apply,
                                                AssetManager* assets);
    static std::optional<std::string> CompactPrefabSource(std::string_view original, std::string_view edited,
                                                          AssetManager* assets);
    static std::optional<std::string> UnpackPrefab(const Entity& root);
    static bool ExpandPrefabs(nlohmann::json& document, AssetManager* assets, std::vector<PrefabLoadGroup>& groups);
    static void RestorePrefabGroups(Scene& scene, std::vector<PrefabLoadGroup>& groups, const IdMap& ids);
};

} // namespace Spot::Detail
