#pragma once

#include "spot/core/color.h"
#include "spot/render/camera_3d.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Spot {
class Texture;
enum class ParticleDrawFacing { Billboard, Stretched, Horizontal };
enum class ParticleDrawBlend { Alpha, Additive };
struct Particle {
    Vector3 position;
    Vector3 velocity;
    float age = 0;
    float lifetime = 1;
    float size = 1;
    float rotation = 0;
    Color color = Color::White;
};
struct ParticleDrawSettings {
    ParticleDrawFacing facing = ParticleDrawFacing::Billboard;
    ParticleDrawBlend blend = ParticleDrawBlend::Alpha;
    float velocityStretch = 0.1f;
    float softDistance = 0.0f;
    std::uint32_t flipbookColumns = 1;
    std::uint32_t flipbookRows = 1;
    float flipbookCycles = 1.0f;
};
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
    ParticleDrawBlend blend = ParticleDrawBlend::Alpha;
    float depth = 0;
    std::size_t sequence = 0;
};

// Converts plain particle arrays to camera-facing instances without graphics resources, reusable by custom renderers.
class ParticleBatch {
public:
    void Clear() { m_draws.clear(); }
    void Add(std::span<const Particle> particles, const ParticleDrawSettings& settings, const Camera3D& camera,
             const Matrix4& emitter = {}, const Texture* texture = nullptr);
    void Sort();
    std::span<const ParticleDraw> GetDraws() const { return m_draws; }

private:
    std::vector<ParticleDraw> m_draws;
};
} // namespace Spot
