#include "spot/graphics/particle_batch.h"
#include <algorithm>
#include <cmath>

namespace Spot {
namespace {
Vector3 Apply(const Matrix4& matrix, Vector3 value, float w) {
    const auto result = matrix * Vector4{value.x, value.y, value.z, w};
    return {result.x, result.y, result.z};
}
Vector4 Pack(Vector3 value, float w = 0) {
    return {value.x, value.y, value.z, w};
}
} // namespace
void ParticleBatch::Add(std::span<const Particle> particles, const ParticleSettings& settings, const Camera3D& camera,
                        const Matrix4& emitter, const Texture* texture) {
    const auto cameraRight = camera.GetRight(), cameraUp = camera.GetUp(), forward = camera.GetForward();
    const auto columns = std::max(settings.flipbookColumns, 1u), rows = std::max(settings.flipbookRows, 1u);
    const auto frames = columns * rows;
    float scale = 1;
    if (settings.space == ParticleSpace::Local) {
        scale = std::max({Length(Apply(emitter, {1, 0, 0}, 0)), Length(Apply(emitter, {0, 1, 0}, 0)),
                          Length(Apply(emitter, {0, 0, 1}, 0))});
    }
    for (const auto& particle : particles) {
        if (particle.size <= 0 || particle.color.a <= 0)
            continue;
        const bool local = settings.space == ParticleSpace::Local;
        const auto position = local ? Apply(emitter, particle.position, 1) : particle.position;
        const auto velocity = local ? Apply(emitter, particle.velocity, 0) : particle.velocity;
        const float depth = Dot(position - camera.position, forward);
        if (depth + particle.size * scale < camera.nearPlane || depth - particle.size * scale > camera.farPlane)
            continue;
        Vector3 right = cameraRight, up = cameraUp;
        if (settings.facing == ParticleFacing::Stretched) {
            const auto projected = velocity - forward * Dot(velocity, forward);
            if (LengthSquared(projected) > 1e-8f) {
                up = Normalize(projected);
                right = Normalize(Cross(forward, up));
            }
            up *= 1 + Length(velocity) * settings.velocityStretch / std::max(particle.size * scale, 0.001f);
        } else if (settings.facing == ParticleFacing::Horizontal) {
            // As a billboard seen from straight above, so its front faces up.
            const float cosine = std::cos(particle.rotation), sine = std::sin(particle.rotation);
            right = {cosine, 0, -sine};
            up = {-sine, 0, -cosine};
        } else {
            const float cosine = std::cos(particle.rotation), sine = std::sin(particle.rotation);
            right = cameraRight * cosine + cameraUp * sine;
            up = cameraUp * cosine - cameraRight * sine;
        }
        const float normalized = std::clamp(particle.age / std::max(particle.lifetime, 0.001f), 0.0f, 1.0f);
        const float progress = normalized * settings.flipbookCycles;
        // The final frame holds at age one; earlier cycles wrap.
        const float phase =
            progress >= settings.flipbookCycles && settings.flipbookCycles > 0 ? 1.0f : progress - std::floor(progress);
        const auto frame = std::min(static_cast<std::uint32_t>(phase * static_cast<float>(frames)), frames - 1);
        const float x = static_cast<float>(frame % columns) / static_cast<float>(columns);
        const float y = static_cast<float>(frame / columns) / static_cast<float>(rows);
        m_draws.push_back({{Pack(position, particle.size * scale),
                            Pack(right, settings.softDistance),
                            Pack(up),
                            particle.color,
                            {x, y, x + 1.0f / static_cast<float>(columns), y + 1.0f / static_cast<float>(rows)}},
                           texture,
                           settings.blend,
                           depth,
                           m_draws.size()});
    }
}
void ParticleBatch::Sort() {
    std::sort(m_draws.begin(), m_draws.end(), [](const ParticleDraw& a, const ParticleDraw& b) {
        if (a.blend != b.blend)
            return a.blend == ParticleBlend::Alpha;
        if (a.blend == ParticleBlend::Alpha && a.depth != b.depth)
            return a.depth > b.depth;
        return a.sequence < b.sequence;
    });
}
} // namespace Spot
