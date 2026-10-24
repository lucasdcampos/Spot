#pragma once

namespace Spot {
class AssetManager;
class GraphicsDevice;

void RegisterRenderAssets(AssetManager& assets, GraphicsDevice* device = nullptr);
} // namespace Spot
