#pragma once

#include <spot.h>

namespace Spot::Editor {
// Preview changes transient particle state only. Selection/reload identities outlive no component pointers.
class ParticlePreview {
public:
    void Select(Scene& scene, EntityId entity, ComponentId component = {}, std::uint64_t revision = 0);
    void Update(Scene& scene, float realDeltaTime);
    void Restart(Scene& scene);
    void Seek(Scene& scene, float seconds);
    void Clear(Scene& scene);
    void SetPlaying(bool playing) { m_playing = playing; }
    bool IsPlaying() const { return m_playing; }
    float GetTime(const Scene& scene) const;
    EntityId GetEntityId() const { return m_entity; }
    ComponentId GetComponentId() const { return m_component; }

private:
    ParticleSystem* Resolve(const Scene& scene) const;
    EntityId m_entity = EntityId::Invalid;
    ComponentId m_component;
    std::uint64_t m_revision = 0;
    bool m_playing = true;
};
} // namespace Spot::Editor
