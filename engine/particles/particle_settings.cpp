#include "particles/particle_settings.h"
#include "core/archive.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Spot {
namespace {
float Finite(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}
void Range(ParticleRange& range, float minimum, float maximum) {
    range.min = std::clamp(Finite(range.min, minimum), minimum, maximum);
    range.max = std::clamp(Finite(range.max, range.min), range.min, maximum);
}
} // namespace
void ParticleRange::Serialize(Archive& archive) {
    archive.Field("min", min);
    archive.Field("max", max);
}
void ParticleBurst::Serialize(Archive& archive) {
    archive.Field("time", time, {.min = 0, .max = 3600});
    archive.Field("count", count, {.min = 0, .max = 100000});
}
void ParticleSettings::Validate() {
    maxParticles = std::clamp(maxParticles, 1u, CapacityLimit);
    duration = std::clamp(Finite(duration, 5), 0.01f, 3600.0f);
    emissionRate = std::clamp(Finite(emissionRate, 0), 0.0f, 100000.0f);
    radius = std::clamp(Finite(radius, 1), 0.0f, 10000.0f);
    coneAngle = std::clamp(Finite(coneAngle, 0.4f), 0.0f, 1.5707963f);
    for (float* axis : {&boxSize.x, &boxSize.y, &boxSize.z})
        *axis = std::clamp(Finite(*axis, 1), 0.0f, 10000.0f);
    for (float* axis : {&gravity.x, &gravity.y, &gravity.z})
        *axis = std::clamp(Finite(*axis, 0), -10000.0f, 10000.0f);
    Range(lifetime, 0.001f, 3600);
    Range(speed, 0, 10000);
    Range(size, 0, 10000);
    Range(rotation, -10000, 10000);
    for (float* channel : {&startColor.r, &startColor.g, &startColor.b, &startColor.a})
        *channel = Finite(*channel, 1);
    if (shape < ParticleShape::Point || shape > ParticleShape::Box)
        shape = ParticleShape::Point;
    if (space != ParticleSpace::Local && space != ParticleSpace::World)
        space = ParticleSpace::World;
    if (facing != ParticleFacing::Billboard && facing != ParticleFacing::Stretched)
        facing = ParticleFacing::Billboard;
    if (blend != ParticleBlend::Alpha && blend != ParticleBlend::Additive)
        blend = ParticleBlend::Alpha;
    velocityStretch = std::clamp(Finite(velocityStretch, 0), 0.0f, 100.0f);
    softDistance = std::clamp(Finite(softDistance, 0), 0.0f, 1000.0f);
    flipbookColumns = std::clamp(flipbookColumns, 1u, 256u);
    flipbookRows = std::clamp(flipbookRows, 1u, 256u);
    flipbookCycles = std::clamp(Finite(flipbookCycles, 1), 0.0f, 1000.0f);
    if (bursts.size() > 1024)
        bursts.resize(1024);
    for (auto& burst : bursts) {
        burst.time = std::clamp(Finite(burst.time, 0), 0.0f, duration);
        burst.count = std::min(burst.count, CapacityLimit);
    }
    std::stable_sort(bursts.begin(), bursts.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
}
void ParticleSettings::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 4> Shapes{"Point", "Sphere", "Cone", "Box"};
    static constexpr std::array<std::string_view, 2> Spaces{"Local", "World"}, Facings{"Billboard", "Stretched"},
        Blends{"Alpha", "Additive"};
    archive.Field("maxParticles", maxParticles, {.min = 1, .max = 100000});
    archive.Field("seed", seed);
    archive.Field("duration", duration, {.min = 0.01f, .max = 3600});
    archive.Field("loop", loop);
    archive.Field("playOnStart", playOnStart);
    archive.Field("emissionRate", emissionRate, {.min = 0, .max = 100000});
    archive.Field("bursts", bursts);
    archive.Field("shape", shape, {.enumNames = Shapes});
    archive.Field("space", space, {.enumNames = Spaces});
    archive.Field("radius", radius);
    archive.Field("coneAngle", coneAngle, {.angle = true});
    archive.Field("boxSize", boxSize);
    archive.Field("lifetime", lifetime);
    archive.Field("speed", speed);
    archive.Field("size", size);
    archive.Field("rotation", rotation);
    archive.Field("startColor", startColor, {.hdr = true});
    archive.Field("sizeOverLifetime", sizeOverLifetime);
    archive.Field("speedOverLifetime", speedOverLifetime);
    archive.Field("rotationOverLifetime", rotationOverLifetime);
    archive.Field("colorOverLifetime", colorOverLifetime);
    archive.Field("gravity", gravity);
    archive.Field("facing", facing, {.enumNames = Facings});
    archive.Field("blend", blend, {.enumNames = Blends});
    archive.Field("velocityStretch", velocityStretch);
    archive.Field("softDistance", softDistance);
    archive.Field("flipbookColumns", flipbookColumns);
    archive.Field("flipbookRows", flipbookRows);
    archive.Field("flipbookCycles", flipbookCycles);
    if (archive.IsLoading())
        Validate();
}
} // namespace Spot
