#include "scene/particle_preview.h"

namespace Spot::Editor {
ParticleSystem* ParticlePreview::Resolve(const Scene& scene) const {
    auto* entity = scene.GetEntity(m_entity);
    if (!entity)
        return nullptr;
    ParticleSystem* found = nullptr;
    entity->ForEachComponent<ParticleSystem>([&](ParticleSystem& effect) {
        if (effect.GetId() == m_component)
            found = &effect;
    });
    return found;
}
void ParticlePreview::Select(Scene& scene, EntityId entityId, ComponentId componentId, std::uint64_t revision) {
    if (!componentId.IsValid() && m_entity == entityId && m_revision == revision && Resolve(scene))
        return;
    auto* entity = scene.GetEntity(entityId);
    ParticleSystem* effect = nullptr;
    if (entity)
        entity->ForEachComponent<ParticleSystem>([&](ParticleSystem& candidate) {
            if ((!componentId.IsValid() && !effect) || candidate.GetId() == componentId)
                effect = &candidate;
        });
    if (!effect) {
        Clear(scene);
        return;
    }
    if (m_entity == entityId && m_component == effect->GetId() && m_revision == revision)
        return;
    Clear(scene);
    m_entity = entityId;
    m_component = effect->GetId();
    m_revision = revision;
    effect->Seek(0);
    m_playing = true;
}
void ParticlePreview::Update(Scene& scene, float realDeltaTime) {
    auto* effect = Resolve(scene);
    if (!effect) {
        Clear(scene);
        return;
    }
    if (m_playing && effect->IsActiveAndEnabled())
        effect->Simulate(realDeltaTime);
}
void ParticlePreview::Restart(Scene& scene) {
    if (auto* effect = Resolve(scene))
        effect->Seek(0);
}
void ParticlePreview::Seek(Scene& scene, float seconds) {
    if (auto* effect = Resolve(scene))
        effect->Seek(seconds);
}
void ParticlePreview::Clear(Scene& scene) {
    if (auto* effect = Resolve(scene))
        effect->Stop(true);
    m_entity = EntityId::Invalid;
    m_component = {};
    m_playing = true;
}
float ParticlePreview::GetTime(const Scene& scene) const {
    const auto* effect = Resolve(scene);
    return effect ? static_cast<float>(effect->GetSimulation().GetTime()) : 0;
}
} // namespace Spot::Editor
