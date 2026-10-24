#include "spot/scene/rendering/clouds.h"

#include "spot/core/io/archive.h"

#include <algorithm>
#include <cmath>

namespace Spot {

SkyClouds Clouds::GetSkyClouds() const {
    return {.coverage = std::clamp(coverage, 0.0f, 1.0f),
            .density = std::clamp(density, 0.0f, 1.0f),
            .height = std::max(height, 1.0f),
            .size = std::max(size, 1.0f),
            .softness = std::clamp(softness, 0.0f, 1.0f),
            .offset = m_offset,
            .color = color};
}

void Clouds::OnUpdate(float deltaTime) {
    const Vector2 wind{windDirection.x, windDirection.z};
    const float length = Length(wind);
    if (length > 1e-6f && std::isfinite(windSpeed)) {
        m_offset += wind / length * (windSpeed * deltaTime);
    }
}

void Clouds::Serialize(Archive& archive) {
    archive.Field("coverage", coverage, {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "0 clear, 1 overcast"});
    archive.Field("density", density,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "How opaque their thick parts are"});
    archive.Field("height", height,
                  {.min = 1.0f, .max = 20000.0f, .speed = 10.0f, .tooltip = "Meters above the viewer"});
    archive.Field("size", size,
                  {.min = 1.0f, .max = 20000.0f, .speed = 5.0f, .tooltip = "About how wide one is, in meters"});
    archive.Field("softness", softness, {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "0 crisp, 1 wispy"});
    archive.Field("windDirection", windDirection, {.speed = 0.01f, .tooltip = "Where the wind blows, on the ground"});
    archive.Field("windSpeed", windSpeed, {.min = 0.0f, .max = 500.0f, .speed = 0.1f, .tooltip = "Meters per second"});
    archive.Field("color", color);
    archive.Field("offset", m_offset, {.hidden = true});
}

} // namespace Spot
