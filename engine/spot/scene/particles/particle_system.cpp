#include "spot/scene/particles/particle_system.h"
#include "spot/assets/asset_archive.h"
#include "spot/core/debug/profiler.h"
#include "spot/core/io/archive.h"
#include "spot/core/jobs/job_system.h"
#include "spot/graphics/texture.h"
#include "spot/scene/entity.h"
#include "spot/scene/scene.h"

#include <vector>

namespace Spot {
void ParticleSystem::EnsureSystem() {
    if (!GetScene().GetSystem<ParticleSceneSystem>())
        GetScene().AddSystem<ParticleSceneSystem>();
}
void ParticleSystem::OnEnable() {
    EnsureSystem();
    if (!m_initialized) {
        m_initialized = true;
        if (GetSettings().playOnStart)
            m_simulation.Play();
        else
            m_simulation.Stop();
    }
}
void ParticleSystem::Play() {
    m_initialized = true;
    EnsureSystem();
    m_simulation.Play();
}
void ParticleSystem::Restart() {
    m_initialized = true;
    EnsureSystem();
    m_simulation.Restart();
}
void ParticleSystem::Emit(std::uint32_t count) {
    m_initialized = true;
    EnsureSystem();
    m_simulation.Emit(count, GetEntity().GetTransform().GetWorldMatrix());
}
void ParticleSystem::Simulate(float seconds) {
    m_simulation.Advance(seconds, GetEntity().GetTransform().GetWorldMatrix());
}
void ParticleSystem::Seek(float seconds) {
    m_simulation.Seek(seconds, GetEntity().GetTransform().GetWorldMatrix());
}
void ParticleSystem::Serialize(Archive& archive) {
    auto settings = GetSettings();
    archive.Field("settings", settings);
    archive.Field("texture", m_texture);
    // Saving does not mutate simulation. The inspector loads every frame; reset only on an actual edit.
    if (archive.IsLoading() && settings != GetSettings())
        SetSettings(std::move(settings));
}
void ParticleSceneSystem::OnUpdate(float deltaTime) {
    SPOT_PROFILE_SCOPE("ParticleSceneSystem");
    // Each system simulates only its own particles, so they run together on the job system's threads, from world
    // transforms read here first, as reading one brings its ancestors' up to date.
    struct Due {
        ParticleSystem* system;
        Matrix4 transform;
    };
    std::vector<Due> due;
    GetScene().ForEachComponent<ParticleSystem>([&](ParticleSystem& system) {
        if (system.IsActiveAndEnabled()) {
            due.push_back({&system, system.GetEntity().GetTransform().GetWorldMatrix()});
        }
    });
    JobSystem::GetShared().ParallelFor(due.size(), 2, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            due[i].system->m_simulation.Advance(deltaTime, due[i].transform);
        }
    });
}
} // namespace Spot
