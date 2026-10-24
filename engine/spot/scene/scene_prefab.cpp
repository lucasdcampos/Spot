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

} // namespace Spot
