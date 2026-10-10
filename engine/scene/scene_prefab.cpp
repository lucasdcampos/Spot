#include "scene/scene.h"

#include "core/log.h"
#include "scene/prefab.h"

namespace Spot {

Entity* Scene::Instantiate(const AssetRef<Prefab>& prefab, const InstantiateOptions& options) {
    if (!prefab) {
        Log::GetEngineLogger().Warning("Could not instantiate prefab {}: the asset is not loaded",
                                       prefab.GetId().ToString());
        return nullptr;
    }
    InstantiateOptions placement = options;
    placement.keepIds = false;
    placement.keepExternalReferences = false;
    const auto roots = Instantiate(prefab->GetEntityText(), placement);
    return roots.empty() ? nullptr : roots.front();
}

} // namespace Spot
