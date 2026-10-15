#include "assets/assets.h"
#include "assets/asset_archive.h"

#include "core/log.h"

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

void Detail::SetAssetManager(AssetManager* manager) {
    s_manager = manager;
}

} // namespace Spot
