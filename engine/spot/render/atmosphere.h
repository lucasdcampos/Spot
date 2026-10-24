#pragma once

#include "spot/core/math/color.h"
#include "spot/core/math/vector.h"

namespace Spot::Detail {

// The atmosphere Renderer3D draws skies with (render/shaders/sky.frag.hlsl), computed on the CPU so the scene can be
// lit the way the sky looks. Its constants must match the shader's.
struct Atmosphere {
    float thickness = 1.0f;
    Color skyTint = Color::White;

    // The share of each color of sunlight that crosses the air down to the viewer: nearly all of it at noon, mostly
    // red at sunset and none once the sun's disc, of this angular radius, has set.
    Color GetSunTransmittance(Vector3 toSun, float sunAngularRadius) const;
    // The light the whole sky casts onto a surface facing up, for a sun of irradiance 1, as an ambient light.
    Color GetSkyIrradiance(Vector3 toSun) const;
    // The light the sky sends toward the viewer from a direction above the horizon, for a sun of irradiance 1.
    Color GetSkyRadiance(Vector3 direction, Vector3 toSun) const;
};

} // namespace Spot::Detail
