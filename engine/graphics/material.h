#pragma once

#include "graphics/color.h"

#include <memory>

namespace Spot {

class Texture;

// How Renderer3D shades a mesh.
struct Material {
    Color baseColor = Color::White;
    // Multiplied by baseColor; without one, the surface is baseColor alone.
    std::shared_ptr<const Texture> baseColorTexture = nullptr;
    // Draws back faces too, for thin surfaces such as leaves or cloth.
    bool doubleSided = false;
};

} // namespace Spot
