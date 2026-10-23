#pragma once

#include "spot/core/matrix.h"
#include "spot/particles/particle_settings.h"
#include <span>

namespace Spot {
struct Particle {
    Vector3 position;
    Vector3 velocity;
    float age = 0;
    float lifetime = 1;
    float startSize = 1;
    float startRotation = 0;
    float size = 1;
    float rotation = 0;
    Color color;
    Vector3 initialVelocity;
    Vector3 gravityVelocity;
};

// Owns contiguous bounded storage. No scene, graphics device or global random state is required.
class ParticleSimulation {
public:
    static constexpr double TimeStep = 1.0 / 120.0;
    explicit ParticleSimulation(const ParticleSettings& settings = {});
    const ParticleSettings& GetSettings() const { return m_settings; }
    void SetSettings(const ParticleSettings& settings);
    void Restart();
    void Stop(bool clear = false);
    void Play() { m_emitting = true; }
    bool IsEmitting() const { return m_emitting; }
    bool IsAlive() const { return m_emitting || !m_particles.empty(); }
    double GetTime() const { return m_time; }
    std::span<const Particle> GetParticles() const { return m_particles; }
    std::uint64_t GetDroppedCount() const { return m_dropped; }
    void Emit(std::uint32_t count, const Matrix4& emitter = {});
    // At most one second per call; excess time is dropped to bound stalled-frame work.
    void Advance(float seconds, const Matrix4& emitter = {});
    // Deterministic replay at a fixed emitter pose, bounded to 60 seconds for editor responsiveness.
    void Seek(float seconds, const Matrix4& emitter = {});

private:
    float Random();
    float Sample(ParticleRange range);
    void Step(const Matrix4& emitter);
    void Integrate(Particle& particle, float seconds);
    ParticleSettings m_settings;
    std::vector<Particle> m_particles;
    std::uint32_t m_random = 1;
    std::uint64_t m_dropped = 0;
    std::uint64_t m_step = 0;
    double m_time = 0;
    double m_pending = 0;
    double m_emission = 0;
    bool m_emitting = true;
};
} // namespace Spot
