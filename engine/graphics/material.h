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
    // When positive, the texture is projected onto the surface from world space instead of following the mesh's UVs,
    // repeating every this many units, so it keeps its size however the mesh is scaled. Suits
    // BuiltInAssets::GridTexture on level blocks.
    float worldTextureSize = 0.0f;
};

} // namespace Spot
