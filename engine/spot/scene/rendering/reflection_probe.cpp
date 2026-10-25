#include "spot/scene/rendering/reflection_probe.h"

#include "spot/core/io/archive.h"
#include "spot/scene/entity.h"

#include <atomic>
#include <cmath>

namespace Spot {

namespace {

// Probes are told apart by an id of their own, which renderers keep their captures by.
std::atomic<std::uint64_t> s_nextId{1};

} // namespace

ReflectionProbe::ReflectionProbe() : m_id(s_nextId.fetch_add(1)) {
}

ReflectionProbeData ReflectionProbe::GetProbeData() const {
    const Vector3 position = GetEntity().GetTransform().GetWorldPosition();
    const Vector3 half{std::abs(size.x) * 0.5f, std::abs(size.y) * 0.5f, std::abs(size.z) * 0.5f};
    return {.id = m_id,
            .position = position,
            .boxMin = position - half,
            .boxMax = position + half,
            .blendDistance = blendDistance,
            .intensity = intensity,
            .version = m_version};
}

void ReflectionProbe::Serialize(Archive& archive) {
    archive.Field(
        "size", size,
        {.min = 0.0f, .max = 10000.0f, .speed = 0.1f, .tooltip = "The box it lights, centered on its entity"});
    archive.Field("blendDistance", blendDistance,
                  {.min = 0.0f, .max = 100.0f, .speed = 0.05f, .tooltip = "How far inside the box its light fades in"});
    archive.Field("intensity", intensity, {.min = 0.0f, .max = 16.0f, .speed = 0.01f});
}

} // namespace Spot
