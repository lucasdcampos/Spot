#pragma once

#include "assets/asset_manager.h"

#include <string_view>

// Static access to the running application's assets, so components and scenes can load them without reaching for
// the application:
//
//     auto texture = Spot::Assets::Load<Spot::Texture>("textures/crate.png");
namespace Spot::Assets {

// Null outside Application::Run().
AssetManager* GetManager();

// Return a reference without an asset, after logging why, when there is no manager or the asset cannot load.
template <LoadableAsset T>
AssetRef<T> Load(AssetId id) {
    AssetManager* manager = GetManager();
    return manager ? manager->Load<T>(id) : AssetRef<T>(id, nullptr);
}

template <LoadableAsset T>
AssetRef<T> Load(std::string_view path) {
    AssetManager* manager = GetManager();
    return manager ? manager->Load<T>(path) : AssetRef<T>();
}

} // namespace Spot::Assets

namespace Spot::Detail {

// The application sets its manager for the duration of Run().
void SetAssetManager(AssetManager* manager);

} // namespace Spot::Detail
