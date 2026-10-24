#pragma once

#include <spot.h>
#include <spot/assets/script_assets.h>

namespace Spot::Tests {
class RegisteredAssetManager : public Spot::AssetManager {
public:
    explicit RegisteredAssetManager(std::filesystem::path path, GraphicsDevice* device = nullptr)
        : Spot::AssetManager(std::move(path)) {
        RegisterGraphicsAssets(*this, device);
        RegisterRenderAssets(*this, device);
        RegisterAudioAssets(*this);
        RegisterAnimationAssets(*this);
        RegisterScriptAssets(*this);
        RegisterSceneAssets(*this);
    }
};
} // namespace Spot::Tests
