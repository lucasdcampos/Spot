#pragma once

#include "spot/graphics/camera_3d.h"
#include "spot/particles/particle_simulation.h"
#include <vector>

namespace Spot {
class Texture;
struct ParticleInstance {
    Vector4 centerAndSize;
    Vector4 rightAndSoftDistance;
    Vector4 up;
    Color color;
    Vector4 uvRect;
};
struct ParticleDraw {
    ParticleInstance instance;
    const Texture* texture = nullptr;
    ParticleBlend blend = ParticleBlend::Alpha;
    float depth = 0;
    std::size_t sequence = 0;
};

// Converts simulation data to camera-facing instances without graphics resources, reusable by custom renderers.
class ParticleBatch {
public:
    void Clear() { m_draws.clear(); }
    void Add(std::span<const Particle> particles, const ParticleSettings& settings, const Camera3D& camera,
             const Matrix4& emitter = {}, const Texture* texture = nullptr);
    void Sort();
    std::span<const ParticleDraw> GetDraws() const { return m_draws; }

private:
    std::vector<ParticleDraw> m_draws;
};
} // namespace Spot
