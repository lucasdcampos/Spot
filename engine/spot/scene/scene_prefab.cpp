#include "spot/scene/scene.h"

#include "spot/core/debug/log.h"
#include "spot/scene/prefab.h"
#include "spot/scene/scene_serializer.h"

namespace Spot {

Entity* Scene::Instantiate(const AssetRef<Prefab>& prefab, const InstantiateOptions& options) {
    if (!prefab) {
        Log::GetEngineLogger().Warning("Could not instantiate prefab {}: the asset is not loaded",
                                       prefab.GetId().ToString());
        return nullptr;
    }
    return Detail::SceneSerializer::InstantiatePrefab(*this, prefab, options);
}

AssetId Entity::GetPrefabAssetId() const {
    return m_prefabInstance ? m_prefabInstance->asset.GetId() : AssetId();
}

std::vector<PrefabOverride> GetPrefabOverrides(const Entity& root) {
    return Detail::SceneSerializer::GetOverrides(root);
}

std::optional<PrefabEdit> PreparePrefabEdit(const Entity& root, const PrefabOverrideScope& scope, bool apply,
                                            AssetManager* assets) {
    return Detail::SceneSerializer::EditPrefab(root, scope, apply, assets);
}

std::optional<std::string> UnpackPrefabToString(const Entity& root) {
    return Detail::SceneSerializer::UnpackPrefab(root);
}

} // namespace Spot
