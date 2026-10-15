#include "scene/particles/particle_system.h"
#include "assets/asset_archive.h"
#include "core/archive.h"
#include "graphics/texture.h"
#include "scene/entity.h"
#include "scene/scene.h"

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
    GetScene().ForEachComponent<ParticleSystem>([&](ParticleSystem& system) {
        if (system.IsActiveAndEnabled())
            system.Simulate(deltaTime);
    });
}
} // namespace Spot
