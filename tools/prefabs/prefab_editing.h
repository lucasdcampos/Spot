#pragma once

// Editing prefab instances: their overrides, applying them to the prefab, reverting and unpacking them, and saving
// scenes as asset files keep them. Games only instantiate prefabs; how instances are saved is described in
// <spot/scene/prefab_document.h>.

#include <spot/assets/asset_manager.h>
#include <spot/scene/scene.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

// The whole instance when entity is zero; otherwise the entity with that id in the prefab, and the subtree of its
// keyed form that the JSON pointer selects.
struct PrefabOverrideScope {
    std::uint64_t entity = 0;
    std::string pointer;
};

struct PrefabOverride {
    PrefabOverrideScope scope;
    // A JSON patch operation, "remove entity" or "add entity".
    std::string operation;
};

// Prepared documents let tools write the asset and replace the scene as one undoable operation.
struct PrefabEdit {
    std::string sceneText;
    // Empty for a revert. Applying makes a validated source tree with source-local references.
    std::string prefabText;
};

// The scene, or the entity and its descendants, as asset files keep them: each prefab instance as its prefab and the
// overrides of its entities, which loading applies to the prefab as it is then. A prefab source saved from an entity
// keeps the prefabs nested in it as such.
std::string SaveSceneSource(const Scene& scene, AssetManager* assets = nullptr);
std::string SaveEntitySource(const Entity& entity, AssetManager* assets = nullptr);

// The changes of the instance whose root this is from its prefab.
std::vector<PrefabOverride> GetPrefabOverrides(const Entity& root, AssetManager* assets = nullptr);
// The scene without the selected changes, and, when applying, the prefab's source with them. prefabSource is the
// text of the prefab's source file, which keeps the prefabs nested in it.
std::optional<PrefabEdit> PreparePrefabEdit(const Entity& root, const PrefabOverrideScope& scope = {},
                                            bool apply = false, std::string_view prefabSource = {},
                                            AssetManager* assets = nullptr);
// The scene with just this instance made of ordinary entities; other instances keep their links and overrides.
std::optional<std::string> UnpackPrefabToString(const Entity& root);

} // namespace Spot
