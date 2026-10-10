#include "scene/scene.h"

#include "core/log.h"
#include "scene/prefab.h"
#include "scene/scene_serializer.h"

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

} // namespace Spot
