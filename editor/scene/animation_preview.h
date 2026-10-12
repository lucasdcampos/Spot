#pragma once

#include <spot.h>

namespace Spot::Editor {
// Preview changes transient animation state only. Selection/reload identities outlive no component pointers.
class AnimationPreview {
public:
    void Select(Scene& scene, EntityId entity, ComponentId component = {}, std::uint64_t revision = 0);
    void Update(Scene& scene, float realDeltaTime);
    void Restart(Scene& scene);
    void SetClip(AssetRef<AnimationClip> clip);
    void Seek(Scene& scene, float seconds);
    void Clear(Scene& scene);
    void SetPlaying(bool playing) { m_playing = playing; }
    bool IsPlaying() const { return m_playing; }
    float GetTime(const Scene& scene) const;
    EntityId GetEntityId() const { return m_entity; }
    ComponentId GetComponentId() const { return m_component; }

private:
    Animator* Resolve(const Scene& scene) const;
    EntityId m_entity = EntityId::Invalid;
    ComponentId m_component;
    std::uint64_t m_revision = 0;
    bool m_playing = true;
    AssetRef<AnimationClip> m_clip;
    float m_clipTime = 0;
};
} // namespace Spot::Editor
