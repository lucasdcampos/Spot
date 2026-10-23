#pragma once

#include "spot/math/angles.h"
#include "spot/math/color.h"
#include "spot/math/vector.h"

namespace Spot {

enum class LightType {
    // Reaches every surface evenly from all directions, so surfaces facing away from other lights do not go black.
    Ambient,
    // Shines in one direction from infinitely far away, like the sun.
    Directional,
    // Shines in every direction from a position, falling off with the square of the distance until its range.
    Point,
    // Shines in a cone from a position, falling off like a point light and toward the edge of the cone.
    Spot,
};

// A light for Renderer3D::DrawLight(). Each type uses only some of the fields.
struct LightData {
    LightType type = LightType::Directional;
    Color color = Color::White;
    // Multiplies the color. Its unit is what a white matte surface shows: one unit of illuminance makes it show
    // white, before exposure. Ambient and directional lights give their intensity to every surface facing them. Point
    // and spot lights give it at a distance of one unit, and a quarter of it at two units: the illuminance at distance
    // d is intensity / d^2, so as the camera's units are meters, the intensity is a luminous intensity, as candelas
    // are, at the scale exposure 0 puts on light.
    float intensity = 1.0f;
    // Point and spot lights.
    Vector3 position = {};
    // The direction the light travels in, from the light toward the scene, for directional and spot lights.
    Vector3 direction = {0.0f, -1.0f, 0.0f};
    // The distance at which point and spot lights stop lighting anything. The falloff is windowed to reach 0 there
    // smoothly, which changes little up to half of it, so lights can be skipped beyond it.
    float range = 10.0f;
    // The angles from the center of a spot light's cone, in radians: fully lit inside the inner one and fading to
    // nothing at the outer one.
    float innerConeAngle = ToRadians(20.0f);
    float outerConeAngle = ToRadians(30.0f);
    // Whether what Renderer3D draws blocks this light from what is behind it, for directional, point and spot
    // lights. Shadows cost a pass over the scene per directional cascade, spot light and point light face, so
    // Renderer3D only draws them for its first directional light that casts them and for as many of the other lights
    // as its shadow tiles hold; the rest light everything as if nothing were in the way.
    bool castsShadows = false;
};

} // namespace Spot
