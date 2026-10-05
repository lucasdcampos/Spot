#pragma once

#include "assets/asset_ref.h"
#include "graphics/color.h"

namespace Spot {

class Texture;

// How Renderer3D shades a mesh.
struct Material {
    Color baseColor = Color::White;
    // Multiplied by baseColor; without one, the surface is baseColor alone.
    AssetRef<Texture> baseColorTexture = {};
    // Draws back faces too, for thin surfaces such as leaves or cloth.
    bool doubleSided = false;
};

} // namespace Spot
