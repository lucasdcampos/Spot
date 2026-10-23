#include "spot/graphics/atmosphere.h"

#include "spot/core/angles.h"

#include <algorithm>
#include <cmath>

namespace Spot::Detail {

namespace {

// In kilometers; see sky.frag.hlsl, which these must match.
constexpr float PlanetRadius = 6360.0f;
constexpr float AtmosphereRadius = 6420.0f;
constexpr float ViewerHeight = 0.05f;
constexpr float RayleighHeight = 8.0f;
constexpr float MieHeight = 1.2f;
constexpr Vector3 RayleighScattering = {5.802e-3f, 13.558e-3f, 33.1e-3f};
constexpr float MieScattering = 3.996e-3f;
constexpr float MieExtinction = 4.44e-3f;
constexpr float MieAnisotropy = 0.8f;
constexpr int ViewSamples = 16;
constexpr int SunSamples = 8;

constexpr float ViewerRadius = PlanetRadius + ViewerHeight;

float GetExitDistance(float r, float mu, float sphereRadius) {
    const float b = r * mu;
    const float c = (r - sphereRadius) * (r + sphereRadius);
    return -b + std::sqrt(std::max(b * b - c, 0.0f));
}

float GetGroundDistance(float r, float mu) {
    const float b = r * mu;
    const float c = (r - PlanetRadius) * (r + PlanetRadius);
    const float discriminant = b * b - c;
    if (mu >= 0.0f || discriminant < 0.0f) {
        return -1.0f;
    }
    return c / (-b + std::sqrt(discriminant));
}

// The densities of air (x) and haze (y) at the distance r from the planet's center, relative to sea level.
Vector2 GetDensity(float r) {
    const float height = r - PlanetRadius;
    return {std::exp(-height / RayleighHeight), std::exp(-height / MieHeight)};
}

Vector2 GetOpticalDepth(float r, float mu, float distance, int samples) {
    const float step = distance / static_cast<float>(samples);
    Vector2 depth;
    for (int i = 0; i < samples; ++i) {
        const float s = (static_cast<float>(i) + 0.5f) * step;
        depth += GetDensity(std::sqrt(r * r + s * s + 2.0f * r * s * mu)) * step;
    }
    return depth;
}

// The sun is hidden when the planet is in the way.
bool GetSunOpticalDepth(float r, float muSun, Vector2& depth) {
    if (GetGroundDistance(r, muSun) > 0.0f) {
        return false;
    }
    depth = GetOpticalDepth(r, muSun, GetExitDistance(r, muSun, AtmosphereRadius), SunSamples);
    return true;
}

Vector3 Exp(Vector3 v) {
    return {std::exp(v.x), std::exp(v.y), std::exp(v.z)};
}

float Smoothstep(float from, float to, float x) {
    const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Color ToColor(Vector3 v) {
    return {v.x, v.y, v.z, 1.0f};
}

} // namespace

Color Atmosphere::GetSunTransmittance(Vector3 toSun, float sunAngularRadius) const {
    toSun = Normalize(toSun);
    // The horizon of a viewer above the ground is a little below level, and the disc sets gradually across it.
    const float horizonMu = -std::sqrt(1.0f - (PlanetRadius / ViewerRadius) * (PlanetRadius / ViewerRadius));
    const float elevation = std::asin(std::clamp(toSun.y, -1.0f, 1.0f)) - std::asin(horizonMu);
    const float radius = std::max(sunAngularRadius, ToRadians(0.1f));
    const float visible = Smoothstep(-radius, radius, elevation);
    if (visible <= 0.0f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    // Seen through the air just above the horizon while it sets.
    const float mu = std::max(toSun.y, horizonMu + 1e-4f);
    const Vector2 depth = GetOpticalDepth(ViewerRadius, mu, GetExitDistance(ViewerRadius, mu, AtmosphereRadius), 32);
    const Vector3 transmittance = Exp(
        -(RayleighScattering * depth.x + Vector3{MieExtinction, MieExtinction, MieExtinction} * depth.y) * thickness);
    return ToColor(transmittance * visible);
}

Color Atmosphere::GetSkyRadiance(Vector3 direction, Vector3 toSun) const {
    direction = Normalize(direction);
    toSun = Normalize(toSun);
    const float mu = std::max(direction.y, 0.0f);
    const float muSun = toSun.y;
    const float nu = Dot(direction, toSun);
    const Vector3 rayleigh = RayleighScattering * thickness;
    const float mie = MieScattering * thickness;
    const float mieExtinction = MieExtinction * thickness;

    const float distance = GetExitDistance(ViewerRadius, mu, AtmosphereRadius);
    Vector2 viewDepth;
    Vector3 rayleighSum;
    Vector3 mieSum;
    for (int i = 0; i < ViewSamples; ++i) {
        const float start = distance * std::pow(static_cast<float>(i) / ViewSamples, 2.0f);
        const float end = distance * std::pow(static_cast<float>(i + 1) / ViewSamples, 2.0f);
        const float t = (start + end) * 0.5f;
        const float r = std::sqrt(ViewerRadius * ViewerRadius + t * t + 2.0f * ViewerRadius * t * mu);
        const Vector2 density = GetDensity(r) * (end - start);
        Vector2 sunDepth;
        if (GetSunOpticalDepth(r, (ViewerRadius * muSun + t * nu) / r, sunDepth)) {
            const Vector2 total = viewDepth + density * 0.5f + sunDepth;
            const Vector3 transmittance =
                Exp(-(rayleigh * total.x + Vector3{mieExtinction, mieExtinction, mieExtinction} * total.y));
            rayleighSum += transmittance * density.x;
            mieSum += transmittance * density.y;
        }
        viewDepth += density;
    }

    const float rayleighPhase = 3.0f / (16.0f * Pi) * (1.0f + nu * nu);
    const float g2 = MieAnisotropy * MieAnisotropy;
    const float miePhase = 3.0f / (8.0f * Pi) * (1.0f - g2) * (1.0f + nu * nu) /
                           ((2.0f + g2) * std::pow(std::max(1.0f + g2 - 2.0f * MieAnisotropy * nu, 1e-4f), 1.5f));
    const Vector3 tint = {skyTint.r, skyTint.g, skyTint.b};
    return ToColor(rayleighSum * rayleigh * tint * rayleighPhase + mieSum * mie * miePhase);
}

Color Atmosphere::GetSkyIrradiance(Vector3 toSun) const {
    // Directions spread over the sky by the cosine of their angle from straight up, so their plain average, times pi,
    // is what a surface facing up receives.
    constexpr int Rings = 4;
    constexpr int Segments = 8;
    Vector3 sum;
    for (int ring = 0; ring < Rings; ++ring) {
        const float sinTheta = std::sqrt((static_cast<float>(ring) + 0.5f) / Rings);
        const float cosTheta = std::sqrt(1.0f - sinTheta * sinTheta);
        for (int segment = 0; segment < Segments; ++segment) {
            // Each ring is turned half a segment from the previous one, so the directions do not line up.
            const float phi =
                2.0f * Pi * (static_cast<float>(segment) + 0.5f * static_cast<float>(ring % 2 + 1)) / Segments;
            const Color radiance =
                GetSkyRadiance({sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)}, toSun);
            sum += Vector3{radiance.r, radiance.g, radiance.b};
        }
    }
    return ToColor(sum * (Pi / (Rings * Segments)));
}

} // namespace Spot::Detail
