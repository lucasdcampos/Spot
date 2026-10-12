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
    if (!effect && entity) {
        if (auto* skin = entity->GetComponent<SkinnedMeshRenderer>()) {
            if (auto* owner = scene.GetEntity(skin->GetAnimator())) {
                effect = owner->GetComponent<Animator>();
            }
        }
        if (!effect) {
            effect = entity->GetComponentInParent<Animator>();
        }
    }
    if (!effect) {
        Clear(scene);
        return;
    }
    entityId = effect->GetEntity().GetId();
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
    if (effect->IsActiveAndEnabled()) {
        if (m_clip) {
            if (m_playing) {
                m_clipTime = Pose::WrapTime(m_clipTime + realDeltaTime * effect->GetSpeed(), m_clip->duration, true);
            }
            if (!effect->PreviewClip(m_clip, m_clipTime)) {
                m_clip = {};
            }
        } else if (m_playing) {
            effect->Preview(realDeltaTime);
        }
        effect->RefreshRenderers();
    }
}
void AnimationPreview::SetClip(AssetRef<AnimationClip> clip) {
    if (clip != m_clip) {
        m_clip = std::move(clip);
        m_clipTime = 0;
    }
}
void AnimationPreview::Restart(Scene& scene) {
    if (m_clip) {
        Seek(scene, 0);
        return;
    }
    if (auto* effect = Resolve(scene))
        effect->Seek(0);
    if (auto* effect = Resolve(scene))
        effect->RefreshRenderers();
}
void AnimationPreview::Seek(Scene& scene, float seconds) {
    if (m_clip) {
        m_clipTime = Pose::WrapTime(seconds, m_clip->duration, false);
        if (auto* animator = Resolve(scene)) {
            animator->PreviewClip(m_clip, m_clipTime);
            animator->RefreshRenderers();
        }
        return;
    }
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
    m_clip = {};
    m_clipTime = 0;
    m_entity = EntityId::Invalid;
    m_component = {};
    m_playing = true;
}
float AnimationPreview::GetTime(const Scene& scene) const {
    const auto* effect = Resolve(scene);
    return effect ? effect->GetTime() : 0;
}
} // namespace Spot::Editor
