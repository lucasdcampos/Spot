#pragma once

#include "spot/core/asset_id.h"

#include <memory>
#include <string_view>

namespace Spot {

class AssetManager;
class Scene;

void RegisterSceneAssets(AssetManager& assets);

// Scenes are not shared assets: each call makes a new scene from the asset, whose asset references load through the
// manager. Null, after logging why, when the asset is missing or not a scene.
std::unique_ptr<Scene> InstantiateScene(AssetManager& assets, AssetId id);
std::unique_ptr<Scene> InstantiateScene(AssetManager& assets, std::string_view path);

namespace Assets {

// Through the running application's asset manager. Null, after logging why, when there is no manager or the scene
// cannot be instantiated.
std::unique_ptr<Scene> InstantiateScene(std::string_view path);

} // namespace Assets

} // namespace Spot
