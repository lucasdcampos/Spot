#include "spot/particles/particle_simulation.h"
#include "spot/core/debug/profiler.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Spot {
namespace {
Vector3 Apply(const Matrix4& matrix, Vector3 vector, float w) {
    const auto result = matrix * Vector4{vector.x, vector.y, vector.z, w};
    return {result.x, result.y, result.z};
}
} // namespace
ParticleSimulation::ParticleSimulation(const ParticleSettings& settings) {
    SetSettings(settings);
}
void ParticleSimulation::SetSettings(const ParticleSettings& settings) {
    m_settings = settings;
    m_settings.Validate();
    m_particles.clear();
    m_particles.reserve(m_settings.maxParticles);
    Restart();
    if (!m_settings.playOnStart)
        Stop();
}
void ParticleSimulation::Restart() {
    m_particles.clear();
    m_random = m_settings.seed ? m_settings.seed : 1;
    m_time = m_pending = m_emission = 0;
    m_step = m_dropped = 0;
    m_emitting = true;
}
void ParticleSimulation::Stop(bool clear) {
    m_emitting = false;
    if (clear)
        m_particles.clear();
}
float ParticleSimulation::Random() {
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return static_cast<float>(m_random >> 8) * (1.0f / 16777216.0f);
}
float ParticleSimulation::Sample(ParticleRange range) {
    return Lerp(range.min, range.max, Random());
}
void ParticleSimulation::Emit(std::uint32_t count, const Matrix4& emitter) {
    const auto available = m_settings.maxParticles - static_cast<std::uint32_t>(m_particles.size());
    m_dropped += count > available ? count - available : 0;
    count = std::min(count, available);
    for (std::uint32_t i = 0; i < count; ++i) {
        Vector3 position{}, direction{0, 1, 0};
        if (m_settings.shape == ParticleShape::Sphere) {
            const float y = Random() * 2 - 1, angle = Random() * 6.2831853f;
            const float ring = std::sqrt(std::max(0.0f, 1 - y * y));
            direction = {ring * std::cos(angle), y, ring * std::sin(angle)};
            position = direction * (m_settings.radius * std::cbrt(Random()));
        } else if (m_settings.shape == ParticleShape::Cone) {
            const float angle = Random() * 6.2831853f, radial = std::sqrt(Random()) * m_settings.radius;
            position = {radial * std::cos(angle), 0, radial * std::sin(angle)};
            const float y = Lerp(std::cos(m_settings.coneAngle), 1.0f, Random());
            const float ring = std::sqrt(std::max(0.0f, 1 - y * y));
            direction = {ring * std::cos(angle), y, ring * std::sin(angle)};
        } else if (m_settings.shape == ParticleShape::Box) {
            position = Vector3{Random() - 0.5f, Random() - 0.5f, Random() - 0.5f} * m_settings.boxSize;
        }
        Vector3 velocity = direction * Sample(m_settings.speed);
        if (m_settings.space == ParticleSpace::World) {
            position = Apply(emitter, position, 1);
            velocity = Apply(emitter, velocity, 0);
        }
        SimulatedParticle particle;
        particle.position = position;
        particle.initialVelocity = particle.velocity = velocity;
        particle.lifetime = Sample(m_settings.lifetime);
        particle.startSize = Sample(m_settings.size);
        particle.startRotation = Sample(m_settings.rotation);
        Integrate(particle, 0);
        m_particles.push_back(particle);
    }
}
void ParticleSimulation::Integrate(SimulatedParticle& particle, float seconds) {
    particle.age += seconds;
    const float age = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
    const float speed = m_settings.speedOverLifetime.Evaluate(age);
    const Vector3 velocity = particle.initialVelocity * speed + particle.gravityVelocity;
    particle.position +=
        (particle.velocity + velocity) * (seconds * 0.5f) + m_settings.gravity * (seconds * seconds * 0.5f);
    particle.gravityVelocity += m_settings.gravity * seconds;
    particle.velocity = velocity + m_settings.gravity * seconds;
    particle.size = std::max(0.0f, particle.startSize * m_settings.sizeOverLifetime.Evaluate(age));
    particle.rotation = particle.startRotation + m_settings.rotationOverLifetime.Evaluate(age);
    const auto color = m_settings.colorOverLifetime.Evaluate(age);
    const auto start = m_settings.startColor;
    particle.color = {start.r * color.r, start.g * color.g, start.b * color.b, start.a * color.a};
}
void ParticleSimulation::Step(const Matrix4& emitter) {
    const double begin = m_time;
    m_time = static_cast<double>(++m_step) * TimeStep;
    for (auto& particle : m_particles)
        Integrate(particle, static_cast<float>(TimeStep));
    // Swap removal keeps storage dense; rendering performs its own stable depth ordering.
    for (std::size_t i = 0; i < m_particles.size();) {
        if (m_particles[i].age >= m_particles[i].lifetime) {
            m_particles[i] = m_particles.back();
            m_particles.pop_back();
        } else
            ++i;
    }
    if (!m_emitting)
        return;
    const double duration = m_settings.duration;
    const auto cycle = static_cast<std::uint64_t>(std::floor(begin / duration));
    const auto lastCycle = static_cast<std::uint64_t>(std::floor(m_time / duration));
    for (auto current = cycle; current <= lastCycle; ++current) {
        if (!m_settings.loop && current > 0)
            break;
        const double base = static_cast<double>(current) * duration;
        for (const auto& burst : m_settings.bursts) {
            const double at = base + burst.time;
            if ((at > begin || (m_step == 1 && at == 0)) && at <= m_time) {
                const auto first = m_particles.size();
                Emit(burst.count, emitter);
                for (std::size_t i = first; i < m_particles.size(); ++i)
                    Integrate(m_particles[i], static_cast<float>(m_time - at));
            }
        }
    }
    const double activeEnd = m_settings.loop ? m_time : std::min(m_time, duration);
    const double activeBegin = m_settings.loop ? begin : std::min(begin, duration);
    const double emitted = m_emission + (activeEnd - activeBegin) * m_settings.emissionRate;
    const auto count = static_cast<std::uint32_t>(std::floor(emitted + 1e-9));
    const double previous = m_emission;
    m_emission = emitted - count;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto first = m_particles.size();
        Emit(1, emitter);
        if (m_particles.size() > first) {
            const double at = activeBegin + (static_cast<double>(i + 1) - previous) / m_settings.emissionRate;
            Integrate(m_particles.back(), static_cast<float>(std::max(0.0, m_time - at)));
        }
    }
    std::erase_if(m_particles, [](const SimulatedParticle& p) { return p.age >= p.lifetime; });
    if (!m_settings.loop && m_time >= duration)
        m_emitting = false;
}
void ParticleSimulation::Advance(float seconds, const Matrix4& emitter) {
    SPOT_PROFILE_SCOPE("ParticleSimulation::Advance");
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    m_pending += std::min(seconds, 1.0f);
    while (m_pending + 1e-9 >= TimeStep) {
        Step(emitter);
        m_pending -= TimeStep;
    }
}
void ParticleSimulation::Seek(float seconds, const Matrix4& emitter) {
    Restart();
    if (!std::isfinite(seconds))
        return;
    double remaining = std::clamp(static_cast<double>(seconds), 0.0, 60.0);
    while (remaining > 0) {
        const auto step = std::min(remaining, 1.0);
        Advance(static_cast<float>(step), emitter);
        remaining -= step;
    }
}
} // namespace Spot
