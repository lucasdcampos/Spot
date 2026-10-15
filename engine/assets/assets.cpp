#include "assets/assets.h"
#include "assets/asset_archive.h"

#include "core/log.h"
#include "scene/scene.h"

namespace Spot {

namespace {

AssetManager* s_manager = nullptr;

} // namespace

void Detail::WarnAssetWithoutId(std::string_view name) {
    Log::GetEngineLogger().Warning("Field '{}' refers to an asset made in code, which is saved as null", name);
}

AssetManager* Assets::GetManager() {
    return s_manager;
}

std::unique_ptr<Scene> Assets::InstantiateScene(std::string_view path) {
    if (!s_manager) {
        Log::GetEngineLogger().Error("Scene '{}' cannot be instantiated without a running application", path);
        return nullptr;
    }
    return s_manager->InstantiateScene(path);
}

void Detail::SetAssetManager(AssetManager* manager) {
    s_manager = manager;
}

} // namespace Spot
