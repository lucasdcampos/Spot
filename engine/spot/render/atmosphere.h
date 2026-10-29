#pragma once

#include "spot/core/math/color.h"
#include "spot/core/math/vector.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Spot::Detail {

// What the atmosphere is integrated with, on the CPU and the GPU alike (Hillaire 2020): how much light crosses the
// air from any height in any direction, and how much light the air scatters more than once toward a point, for a
// sun at any height. Both depend only on the atmosphere's thickness and tint, so they are built once for each and
// uploaded as they are, which keeps what lights the scene and what is drawn the same.
struct AtmosphereTables {
    // By the cosine of the ray's angle from straight up (x) and the height (y), parameterized as Bruneton's (2017).
    static constexpr std::uint32_t TransmittanceWidth = 256;
    static constexpr std::uint32_t TransmittanceHeight = 64;
    // By the cosine of the sun's angle from straight up (x) and the height (y).
    static constexpr std::uint32_t ScatteringSize = 32;

    // Rows from the ground up.
    std::vector<Vector3> transmittance;
    // For a sun of irradiance 1, the light scattered twice or more toward a point from every direction, to be
    // multiplied by the scattering there; rows from the ground up.
    std::vector<Vector3> multipleScattering;
};

// The atmosphere Renderer3D draws skies with (render/shaders/include/sky.hlsl), computed on the CPU so the scene can be
// lit the way the sky looks. Its constants and how it is integrated must match the shader's.
struct Atmosphere {
    float thickness = 1.0f;
    Color skyTint = Color::White;

    // Built the first time they are asked for with this thickness and tint, then kept; safe from any thread.
    std::shared_ptr<const AtmosphereTables> GetTables() const;

    // The share of each color of sunlight that crosses the air down to the viewer: nearly all of it at noon, mostly
    // red at sunset and none once the sun's disc, of this angular radius, has set.
    Color GetSunTransmittance(Vector3 toSun, float sunAngularRadius) const;
    // The light the whole sky casts onto a surface facing up, for a sun of irradiance 1: what a white surface facing
    // up shows lit by it.
    Color GetSkyIrradiance(Vector3 toSun) const;
    // The radiance the sky sends toward the viewer from a direction above the horizon, for a sun of irradiance 1.
    Color GetSkyRadiance(Vector3 direction, Vector3 toSun) const;
};

} // namespace Spot::Detail
