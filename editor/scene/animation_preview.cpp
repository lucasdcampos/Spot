#include "scene/animation_preview.h"

namespace Spot::Editor {
Animator* AnimationPreview::Resolve(const Scene& scene) const {
    auto* entity = scene.GetEntity(m_entity);
    if (!entity)
        return nullptr;
    Animator* found = nullptr;
    entity->ForEachComponent<Animator>([&](Animator& effect) {
        if (effect.GetId() == m_component)
            found = &effect;
    });
    return found;
}
void AnimationPreview::Select(Scene& scene, EntityId entityId, ComponentId componentId, std::uint64_t revision) {
    if (!componentId.IsValid() && m_entity == entityId && m_revision == revision && Resolve(scene))
        return;
    auto* entity = scene.GetEntity(entityId);
    Animator* effect = nullptr;
    if (entity)
        entity->ForEachComponent<Animator>([&](Animator& candidate) {
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
void AnimationPreview::Update(Scene& scene, float realDeltaTime) {
    auto* effect = Resolve(scene);
    if (!effect) {
        Clear(scene);
        return;
    }
    if (m_playing && effect->IsActiveAndEnabled())
        effect->Preview(realDeltaTime);
    effect->RefreshRenderers();
}
void AnimationPreview::Restart(Scene& scene) {
    if (auto* effect = Resolve(scene))
        effect->Seek(0);
    if (auto* effect = Resolve(scene))
        effect->RefreshRenderers();
}
void AnimationPreview::Seek(Scene& scene, float seconds) {
    if (auto* effect = Resolve(scene))
        effect->Seek(seconds);
    if (auto* effect = Resolve(scene))
        effect->RefreshRenderers();
}
void AnimationPreview::Clear(Scene& scene) {
    if (auto* effect = Resolve(scene))
        effect->Seek(0);
    if (auto* effect = Resolve(scene))
        effect->RefreshRenderers();
    m_entity = EntityId::Invalid;
    m_component = {};
    m_playing = true;
}
float AnimationPreview::GetTime(const Scene& scene) const {
    const auto* effect = Resolve(scene);
    return effect ? effect->GetTime() : 0;
}
} // namespace Spot::Editor
