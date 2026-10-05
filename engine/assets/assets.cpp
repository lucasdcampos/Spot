#include "assets/assets.h"

namespace Spot {

namespace {

AssetManager* s_manager = nullptr;

} // namespace

AssetManager* Assets::GetManager() {
    return s_manager;
}

void Detail::SetAssetManager(AssetManager* manager) {
    s_manager = manager;
}

} // namespace Spot
