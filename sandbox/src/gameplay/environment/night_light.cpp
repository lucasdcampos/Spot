#include "gameplay/environment/night_light.h"

#include "gameplay/environment/day_cycle.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(NightLight);

void NightLight::OnLateUpdate(float /*deltaTime*/) {
    auto* light = GetEntity().GetComponent<Spot::Light>();
    if (!light) {
        return;
    }
    const DayCycle* day = GetScene().FindComponent<DayCycle>();
    const float daylight = day ? day->GetDaylight() : 0.0f;
    // Lamps come on at dusk, before the light is gone, and are fully out once the day is bright.
    const float t = std::clamp((0.6f - daylight) / 0.45f, 0.0f, 1.0f);
    const float level = t * t * (3.0f - 2.0f * t);
    light->SetIntensity(intensity * level);
    if (light->IsEnabled() != (level > 0.001f)) {
        light->SetEnabled(level > 0.001f);
    }
}

void NightLight::Serialize(Spot::Archive& archive) {
    archive.Field("intensity", intensity,
                  {.min = 0.0f, .max = 100000.0f, .speed = 0.1f, .tooltip = "The lamp's intensity at night"});
}

} // namespace Gameplay
