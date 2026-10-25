#pragma once

#include "spot/core/math/vector.h"
#include "spot/render/reflection_probe_data.h"
#include "spot/scene/component.h"

#include <cstdint>

namespace Spot {

// Captures the scene around its entity and lights what is inside its box with it, in place of the sky, as rooms and
// caves need: light that bounces off their walls, and reflections of what is in them rather than of the sky. The box
// is centered on the entity, along the world's axes. The scene is captured when the probe is first drawn and again
// when its entity moves; Refresh() captures it again when what is around it changed, such as the time of day. See
// ReflectionProbeData.
class ReflectionProbe final : public Component {
public:
    ReflectionProbe();

    // The size of the box it lights, in world units.
    Vector3 size = {10.0f, 10.0f, 10.0f};
    float blendDistance = ReflectionProbeData{}.blendDistance;
    float intensity = ReflectionProbeData{}.intensity;

    void Refresh() { ++m_version; }

    // The probe as it is now, around its entity's world position.
    ReflectionProbeData GetProbeData() const;

protected:
    void Serialize(Archive& archive) override;

private:
    std::uint64_t m_id;
    std::uint32_t m_version = 0;
};

} // namespace Spot
