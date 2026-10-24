#pragma once

namespace Spot {
class AssetManager;
class GraphicsDevice;

void RegisterGraphicsAssets(AssetManager& assets, GraphicsDevice* device = nullptr);
} // namespace Spot
