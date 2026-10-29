#include "spot/render/atmosphere.h"

#include "spot/core/math/angles.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

namespace Spot::Detail {

namespace {

// In kilometers; see include/sky.hlsl, which these and the integration below must match.
constexpr float PlanetRadius = 6360.0f;
constexpr float AtmosphereRadius = 6420.0f;
constexpr float ViewerHeight = 0.05f;
constexpr float RayleighHeight = 8.0f;
constexpr float MieHeight = 1.6f;
constexpr Vector3 RayleighScattering = {6.96e-3f, 16.27e-3f, 39.72e-3f};
constexpr float MieScattering = 20.0e-3f;
constexpr float MieExtinction = 22.2e-3f;
constexpr float MieAnisotropy = 0.75f;
constexpr Vector3 OzoneAbsorption = {0.650e-3f, 1.881e-3f, 0.085e-3f};
constexpr float OzoneCenter = 25.0f;
constexpr float OzoneWidth = 15.0f;
// What the ground reflects of the light reaching it, which the air then scatters again.
constexpr float GroundAlbedo = 0.4f;
constexpr int ViewSamples = 24;
constexpr int TransmittanceSamples = 40;
constexpr int ScatteringDirections = 64;
constexpr int ScatteringSamples = 20;

constexpr float ViewerRadius = PlanetRadius + ViewerHeight;

using Tables = AtmosphereTables;

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

Vector3 Exp(Vector3 v) {
    return {std::exp(v.x), std::exp(v.y), std::exp(v.z)};
}

Vector3 Multiply(Vector3 a, Vector3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

// How much of what is scattered along a step reaches its start, per unit of scattering: the integral of the
// transmittance over the step.
Vector3 GetStepIntegral(Vector3 extinction, float length) {
    const auto integral = [length](float e) {
        e = std::max(e, 1e-6f);
        return (1.0f - std::exp(-e * length)) / e;
    };
    return {integral(extinction.x), integral(extinction.y), integral(extinction.z)};
}

float Smoothstep(float from, float to, float x) {
    const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Color ToColor(Vector3 v) {
    return {v.x, v.y, v.z, 1.0f};
}

// What the air scatters and takes at the distance r from the planet's center: molecules (Rayleigh), which scatter blue
// the most; haze (Mie), which scatters all colors alike, mostly forward; and ozone, which scatters nothing but takes
// orange, keeping the sky blue overhead while the sun sets.
struct Medium {
    Vector3 rayleigh;
    float mie = 0.0f;
    Vector3 extinction;
};

Medium GetMedium(float r, float thickness, Vector3 tint) {
    const float height = std::max(r - PlanetRadius, 0.0f);
    const float rayleigh = std::exp(-height / RayleighHeight);
    const float mie = std::exp(-height / MieHeight);
    const float ozone = std::max(1.0f - std::abs(height - OzoneCenter) / OzoneWidth, 0.0f);
    return {.rayleigh = Multiply(RayleighScattering, tint) * (rayleigh * thickness),
            .mie = MieScattering * mie * thickness,
            .extinction = (RayleighScattering * rayleigh + Vector3{MieExtinction, MieExtinction, MieExtinction} * mie +
                           OzoneAbsorption * ozone) *
                          thickness};
}

// Texels hold the values at their centers, so the coordinates from 0 to 1 are moved half a texel in from each edge.
float ToTexel(float x, std::uint32_t size) {
    return 0.5f / static_cast<float>(size) + x * (1.0f - 1.0f / static_cast<float>(size));
}

const float TopDistance = std::sqrt(AtmosphereRadius * AtmosphereRadius - PlanetRadius * PlanetRadius);

Vector2 GetTransmittanceUv(float r, float mu) {
    const float rho = std::sqrt(std::max(r * r - PlanetRadius * PlanetRadius, 0.0f));
    const float distance = GetExitDistance(r, mu, AtmosphereRadius);
    const float nearest = AtmosphereRadius - r;
    const float farthest = rho + TopDistance;
    return {ToTexel((distance - nearest) / std::max(farthest - nearest, 1e-6f), Tables::TransmittanceWidth),
            ToTexel(rho / TopDistance, Tables::TransmittanceHeight)};
}

Vector2 GetScatteringUv(float r, float muSun) {
    return {ToTexel(muSun * 0.5f + 0.5f, Tables::ScatteringSize),
            ToTexel((r - PlanetRadius) / (AtmosphereRadius - PlanetRadius), Tables::ScatteringSize)};
}

// Bilinear, clamped to the edges, as the GPU's sampler reads them.
Vector3 Sample(const std::vector<Vector3>& table, std::uint32_t width, std::uint32_t height, Vector2 uv) {
    const float x = std::clamp(uv.x * static_cast<float>(width) - 0.5f, 0.0f, static_cast<float>(width - 1));
    const float y = std::clamp(uv.y * static_cast<float>(height) - 0.5f, 0.0f, static_cast<float>(height - 1));
    const auto x0 = static_cast<std::uint32_t>(x);
    const auto y0 = static_cast<std::uint32_t>(y);
    const std::uint32_t x1 = std::min(x0 + 1, width - 1);
    const std::uint32_t y1 = std::min(y0 + 1, height - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const Vector3 bottom = table[y0 * width + x0] * (1.0f - fx) + table[y0 * width + x1] * fx;
    const Vector3 top = table[y1 * width + x0] * (1.0f - fx) + table[y1 * width + x1] * fx;
    return bottom * (1.0f - fy) + top * fy;
}

// Of the light from a direction whose cosine with straight up is mu, what reaches the point; none when the planet
// is in the way.
Vector3 GetTransmittance(const Tables& tables, float r, float mu) {
    if (GetGroundDistance(r, mu) > 0.0f) {
        return {};
    }
    return Sample(tables.transmittance, Tables::TransmittanceWidth, Tables::TransmittanceHeight,
                  GetTransmittanceUv(r, mu));
}

Vector3 GetMultipleScattering(const Tables& tables, float r, float muSun) {
    return Sample(tables.multipleScattering, Tables::ScatteringSize, Tables::ScatteringSize, GetScatteringUv(r, muSun));
}

float GetRayleighPhase(float nu) {
    return 3.0f / (16.0f * Pi) * (1.0f + nu * nu);
}

// Cornette and Shanks's.
float GetMiePhase(float nu) {
    const float g2 = MieAnisotropy * MieAnisotropy;
    return 3.0f / (8.0f * Pi) * (1.0f - g2) * (1.0f + nu * nu) /
           ((2.0f + g2) * std::pow(std::max(1.0f + g2 - 2.0f * MieAnisotropy * nu, 1e-4f), 1.5f));
}

void BuildTransmittance(Tables& tables, float thickness) {
    tables.transmittance.resize(Tables::TransmittanceWidth * Tables::TransmittanceHeight);
    for (std::uint32_t y = 0; y < Tables::TransmittanceHeight; ++y) {
        const float rho = TopDistance * static_cast<float>(y) / static_cast<float>(Tables::TransmittanceHeight - 1);
        const float r = std::sqrt(rho * rho + PlanetRadius * PlanetRadius);
        for (std::uint32_t x = 0; x < Tables::TransmittanceWidth; ++x) {
            const float nearest = AtmosphereRadius - r;
            const float farthest = rho + TopDistance;
            const float distance = nearest + (farthest - nearest) * static_cast<float>(x) /
                                                 static_cast<float>(Tables::TransmittanceWidth - 1);
            const float mu =
                distance <= 0.0f
                    ? 1.0f
                    : std::clamp((TopDistance * TopDistance - rho * rho - distance * distance) / (2.0f * r * distance),
                                 -1.0f, 1.0f);
            const float step = distance / TransmittanceSamples;
            Vector3 depth;
            for (int i = 0; i < TransmittanceSamples; ++i) {
                const float t = (static_cast<float>(i) + 0.5f) * step;
                depth +=
                    GetMedium(std::sqrt(r * r + t * t + 2.0f * r * t * mu), thickness, {1.0f, 1.0f, 1.0f}).extinction *
                    step;
            }
            tables.transmittance[y * Tables::TransmittanceWidth + x] = Exp(-depth);
        }
    }
}

// Hillaire's (2020): at each height and sun height, the light scattered once toward the point from every direction,
// with the ground's reflection, and the share of the light the air around it scatters again, whose geometric series
// is all the orders after the first, scattered evenly.
void BuildMultipleScattering(Tables& tables, float thickness, Vector3 tint) {
    constexpr std::uint32_t Size = Tables::ScatteringSize;
    tables.multipleScattering.resize(Size * Size);
    constexpr float GoldenAngle = 2.39996323f;
    for (std::uint32_t y = 0; y < Size; ++y) {
        const float r = std::clamp(PlanetRadius + (AtmosphereRadius - PlanetRadius) * static_cast<float>(y) /
                                                      static_cast<float>(Size - 1),
                                   PlanetRadius + 1e-3f, AtmosphereRadius - 1e-3f);
        for (std::uint32_t x = 0; x < Size; ++x) {
            const float muSun = static_cast<float>(x) / static_cast<float>(Size - 1) * 2.0f - 1.0f;
            const Vector3 toSun = {std::sqrt(std::max(1.0f - muSun * muSun, 0.0f)), muSun, 0.0f};
            Vector3 light;
            Vector3 transfer;
            for (int d = 0; d < ScatteringDirections; ++d) {
                const float z = 1.0f - (2.0f * static_cast<float>(d) + 1.0f) / ScatteringDirections;
                const float ring = std::sqrt(std::max(1.0f - z * z, 0.0f));
                const float phi = GoldenAngle * static_cast<float>(d);
                const Vector3 direction = {ring * std::cos(phi), z, ring * std::sin(phi)};
                const float ground = GetGroundDistance(r, direction.y);
                const float distance = ground > 0.0f ? ground : GetExitDistance(r, direction.y, AtmosphereRadius);
                const float step = distance / ScatteringSamples;
                Vector3 throughput = {1.0f, 1.0f, 1.0f};
                Vector3 scattered;
                Vector3 scatteredAgain;
                for (int i = 0; i < ScatteringSamples; ++i) {
                    const Vector3 point = Vector3{0.0f, r, 0.0f} + direction * ((static_cast<float>(i) + 0.5f) * step);
                    const float height = Length(point);
                    const Medium medium = GetMedium(height, thickness, tint);
                    const Vector3 scattering = medium.rayleigh + Vector3{medium.mie, medium.mie, medium.mie};
                    const Vector3 sun = GetTransmittance(tables, height, Dot(point / height, toSun));
                    const Vector3 integral = Multiply(throughput, GetStepIntegral(medium.extinction, step));
                    scattered += Multiply(Multiply(scattering, sun), integral) / (4.0f * Pi);
                    scatteredAgain += Multiply(scattering, integral);
                    throughput = Multiply(throughput, Exp(-medium.extinction * step));
                }
                if (ground > 0.0f) {
                    const Vector3 point = Vector3{0.0f, r, 0.0f} + direction * ground;
                    const float muGround = Dot(Normalize(point), toSun);
                    scattered += Multiply(throughput, GetTransmittance(tables, PlanetRadius, muGround)) *
                                 (std::max(muGround, 0.0f) * GroundAlbedo / Pi);
                }
                light += scattered / ScatteringDirections;
                transfer += scatteredAgain / ScatteringDirections;
            }
            tables.multipleScattering[y * Size + x] = {light.x / (1.0f - std::min(transfer.x, 0.99f)),
                                                       light.y / (1.0f - std::min(transfer.y, 0.99f)),
                                                       light.z / (1.0f - std::min(transfer.z, 0.99f))};
        }
    }
}

Vector3 IntegrateSkyRadiance(const Tables& tables, float thickness, Vector3 tint, Vector3 direction, Vector3 toSun) {
    const float mu = std::max(direction.y, 0.0f);
    const float muSun = toSun.y;
    const float nu = Dot(direction, toSun);
    const float rayleighPhase = GetRayleighPhase(nu);
    const float miePhase = GetMiePhase(nu);
    // Rays along the ground cross hundreds of kilometers of dense air, so the steps start short near the viewer.
    const float distance = GetExitDistance(ViewerRadius, mu, AtmosphereRadius);
    Vector3 throughput = {1.0f, 1.0f, 1.0f};
    Vector3 radiance;
    for (int i = 0; i < ViewSamples; ++i) {
        const float start = distance * std::pow(static_cast<float>(i) / ViewSamples, 2.0f);
        const float end = distance * std::pow(static_cast<float>(i + 1) / ViewSamples, 2.0f);
        const float t = (start + end) * 0.5f;
        const float r = std::sqrt(ViewerRadius * ViewerRadius + t * t + 2.0f * ViewerRadius * t * mu);
        const float pointMuSun = std::clamp((ViewerRadius * muSun + t * nu) / r, -1.0f, 1.0f);
        const Medium medium = GetMedium(r, thickness, tint);
        const Vector3 sun = GetTransmittance(tables, r, pointMuSun);
        const Vector3 multiple = GetMultipleScattering(tables, r, pointMuSun);
        const Vector3 scattered =
            Multiply(medium.rayleigh, sun * rayleighPhase + multiple) + (sun * miePhase + multiple) * medium.mie;
        radiance += Multiply(Multiply(throughput, scattered), GetStepIntegral(medium.extinction, end - start));
        throughput = Multiply(throughput, Exp(-medium.extinction * (end - start)));
    }
    return radiance;
}

std::shared_ptr<const Tables> BuildTables(float thickness, Vector3 tint) {
    auto tables = std::make_shared<Tables>();
    BuildTransmittance(*tables, thickness);
    BuildMultipleScattering(*tables, thickness, tint);
    return tables;
}

} // namespace

std::shared_ptr<const AtmosphereTables> Atmosphere::GetTables() const {
    struct Entry {
        float thickness;
        Vector3 tint;
        std::shared_ptr<const Tables> tables;
    };
    static std::mutex mutex;
    static std::vector<Entry> cache;
    const Vector3 tint = {skyTint.r, skyTint.g, skyTint.b};
    const std::scoped_lock lock(mutex);
    for (const Entry& entry : cache) {
        if (entry.thickness == thickness && entry.tint == tint) {
            return entry.tables;
        }
    }
    // A few are kept, for the skies of a few scenes or a thickness eased between two.
    if (cache.size() >= 4) {
        cache.erase(cache.begin());
    }
    cache.push_back({thickness, tint, BuildTables(thickness, tint)});
    return cache.back().tables;
}

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
    const Vector3 transmittance = Sample(GetTables()->transmittance, Tables::TransmittanceWidth,
                                         Tables::TransmittanceHeight, GetTransmittanceUv(ViewerRadius, mu));
    return ToColor(transmittance * visible);
}

Color Atmosphere::GetSkyRadiance(Vector3 direction, Vector3 toSun) const {
    return ToColor(IntegrateSkyRadiance(*GetTables(), thickness, {skyTint.r, skyTint.g, skyTint.b},
                                        Normalize(direction), Normalize(toSun)));
}

Color Atmosphere::GetSkyIrradiance(Vector3 toSun) const {
    toSun = Normalize(toSun);
    const std::shared_ptr<const Tables> tables = GetTables();
    const Vector3 tint = {skyTint.r, skyTint.g, skyTint.b};
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
            sum += IntegrateSkyRadiance(*tables, thickness, tint,
                                        {sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)}, toSun);
        }
    }
    return ToColor(sum * (Pi / (Rings * Segments)));
}

} // namespace Spot::Detail
