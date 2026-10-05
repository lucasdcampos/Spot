#pragma once

#include "graphics/color.h"
#include "math/angles.h"
#include "math/vector.h"

namespace Spot {

enum class LightType {
    // Reaches every surface evenly from all directions, so surfaces facing away from other lights do not go black.
    Ambient,
    // Shines in one direction from infinitely far away, like the sun.
    Directional,
    // Shines in every direction from a position, fading out by its range.
    Point,
    // Shines in a cone from a position, fading out by its range and toward the edge of the cone.
    Spot,
};

// A light for Renderer3D::DrawLight(). Each type uses only some of the fields.
struct LightData {
    LightType type = LightType::Directional;
    Color color = Color::White;
    // Multiplies the color.
    float intensity = 1.0f;
    // Point and spot lights.
    Vector3 position = {};
    // The direction the light travels in, from the light toward the scene, for directional and spot lights.
    Vector3 direction = {0.0f, -1.0f, 0.0f};
    // The distance at which point and spot lights stop lighting anything.
    float range = 10.0f;
    // The angles from the center of a spot light's cone, in radians: fully lit inside the inner one and fading to
    // nothing at the outer one.
    float innerConeAngle = ToRadians(20.0f);
    float outerConeAngle = ToRadians(30.0f);
};

} // namespace Spot
