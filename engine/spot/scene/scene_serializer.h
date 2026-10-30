#pragma once

#include "spot/core/io/archive_state.h"
#include "spot/scene/prefab.h"
#include "spot/scene/scene.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Spot::Detail {

struct SceneSerializer {
    using IdMap = std::unordered_map<std::uint64_t, EntityId>;
    using PrefabRoots = std::unordered_set<EntityId>;
    // The prefabs of the instance records expanded in a document, by the saved id of each instance's root.
    using PrefabAssets = std::unordered_map<std::uint64_t, AssetRef<Prefab>>;

    static void Serialize(Component& component, Archive& archive);
    static void Validate(Component& component);
    static void CollectPrefabRoots(const Entity& entity, PrefabRoots& roots);
    static void SaveTree(const Entity& entity, nlohmann::json& entities, const PrefabRoots& roots,
                         std::unordered_map<EntityId, IdMap>& localIds);
    static void SaveEntity(const Entity& entity, nlohmann::json& node, const IdMap* prefabIds = nullptr);
    static std::optional<nlohmann::json> SaveComponent(const Component& component, const IdMap* prefabIds = nullptr);
    static std::optional<nlohmann::json> ParseDocument(std::string_view text);
    // A whole scene's file gives its entities their saved ids (Entity::GetSavedId()).
    static std::optional<std::vector<Entity*>> Load(Scene& scene, std::string_view text,
                                                    const InstantiateOptions& options, bool sceneFile = false);
    // Instantiates the document's entities, expanding its prefab records with the prefabs given, or those of the
    // options' asset manager when none are.
    static std::optional<std::vector<Entity*>> Load(Scene& scene, nlohmann::json document,
                                                    const InstantiateOptions& options,
                                                    const Prefab::NestedPrefabs* prefabs = nullptr,
                                                    bool sceneFile = false);
    static Component* LoadComponent(Entity& entity, nlohmann::json& saved, std::optional<std::size_t> index,
                                    const IdMap& ids, const Scene* referenced, AssetManager* assets,
                                    const IdMap* localIds = nullptr, const IdMap* externalIds = nullptr,
                                    bool keepUnknown = true);
    static bool ReplaceComponent(Component& component, std::string_view text, AssetManager* assets);
    static Entity* InstantiatePrefab(Scene& scene, const AssetRef<Prefab>& prefab, const InstantiateOptions& options);
    // Replaces the document's prefab records with the entities they make (see prefab_document.h). Their raw entity
    // references map through "referenceScopes", which the document gets, to the expanded entities' saved ids.
    static bool ExpandPrefabs(nlohmann::json& document, const Prefab::NestedPrefabs& prefabs, PrefabAssets* assets);
};

} // namespace Spot::Detail
