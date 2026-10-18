#pragma once

#include "math/curve.h"
#include "math/gradient.h"
#include "math/vector.h"
#include <cstdint>
#include <vector>

namespace Spot {
class Archive;
enum class ParticleShape { Point, Sphere, Cone, Box };
enum class ParticleSpace { Local, World };
// Billboards face the camera; stretched ones also lengthen along their velocity; horizontal ones lie flat, facing up,
// as ripples and marks on the ground do.
enum class ParticleFacing { Billboard, Stretched, Horizontal };
enum class ParticleBlend { Alpha, Additive };

struct ParticleRange {
    float min = 1.0f;
    float max = 1.0f;
    void Serialize(Archive& archive);
    friend bool operator==(const ParticleRange&, const ParticleRange&) = default;
};
struct ParticleBurst {
    float time = 0.0f;
    std::uint32_t count = 20;
    void Serialize(Archive& archive);
    friend bool operator==(const ParticleBurst&, const ParticleBurst&) = default;
};
struct ParticleSettings {
    static constexpr std::uint32_t CapacityLimit = 100000;
    std::uint32_t maxParticles = 1024;
    std::uint32_t seed = 1;
    float duration = 5.0f;
    bool loop = true;
    bool playOnStart = true;
    float emissionRate = 10.0f;
    std::vector<ParticleBurst> bursts;
    ParticleShape shape = ParticleShape::Point;
    ParticleSpace space = ParticleSpace::World;
    float radius = 1.0f;
    float coneAngle = 0.4f;
    Vector3 boxSize = {1, 1, 1};
    ParticleRange lifetime = {1, 2};
    ParticleRange speed = {1, 2};
    ParticleRange size = {0.1f, 0.2f};
    ParticleRange rotation = {0, 0};
    Color startColor = Color::White;
    Curve sizeOverLifetime;
    Curve speedOverLifetime;
    Curve rotationOverLifetime = Curve(0);
    Gradient colorOverLifetime;
    Vector3 gravity = {0, -9.81f, 0};
    ParticleFacing facing = ParticleFacing::Billboard;
    ParticleBlend blend = ParticleBlend::Alpha;
    float velocityStretch = 0.1f;
    float softDistance = 0.0f;
    std::uint32_t flipbookColumns = 1;
    std::uint32_t flipbookRows = 1;
    float flipbookCycles = 1;
    friend bool operator==(const ParticleSettings&, const ParticleSettings&) = default;
    void Validate();
    void Serialize(Archive& archive);
};
} // namespace Spot
