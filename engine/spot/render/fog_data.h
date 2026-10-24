#pragma once

#include "spot/core/math/color.h"

namespace Spot {

// Fog for Renderer3D::DrawFog(): air that hides what is far away in its color, thicker low down. It hides the sky too
// toward the horizon, so far surfaces and the horizon meet in the same color. Colors are linear, and may be above 1.
struct FogData {
    Color color = {0.6f, 0.66f, 0.74f};
    // How much of the light the fog takes per unit of distance at the base height: 0.01 hides half of what is 70
    // units away.
    float density = 0.01f;
    // How far from the camera the fog starts.
    float start = 0.0f;
    // How fast the fog thins with height above the base height, per unit; 0 keeps it as thick at every height, which
    // hides the whole sky.
    float heightFalloff = 0.05f;
    // The height at which the fog has its density.
    float baseHeight = 0.0f;
};

} // namespace Spot
