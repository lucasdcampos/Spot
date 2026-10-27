#include "gameplay/vehicles/helicopter_downwash.h"

#include "gameplay/environment/water.h"
#include "gameplay/vehicles/helicopter.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(HelicopterDownwash);

void HelicopterDownwash::OnLateUpdate(float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }
    const auto* helicopter = GetEntity().GetComponentInParent<Helicopter>();
    if (!helicopter || !helicopter->IsActiveAndEnabled() || helicopter->GetRotorSpeed() <= 0.05f || range <= 0.0f) {
        m_untilRipple = 0.0f;
        return;
    }
    m_untilRipple -= deltaTime;
    if (m_untilRipple > 0.0f) {
        return;
    }
    // Emit at most once per frame, retaining the remainder without accumulating a burst after a long frame.
    m_untilRipple = std::max(m_untilRipple + std::max(interval, 0.05f), 0.0f);
    const auto* rotor = GetScene().GetEntity(helicopter->mainRotor);
    const auto& transform = (rotor ? *rotor : helicopter->GetEntity()).GetTransform();
    const auto origin = transform.GetWorldPosition();
    const auto direction = transform.GetWorldRotation() * Spot::Vector3{0.0f, -1.0f, 0.0f};
    Water* surface = nullptr;
    float distance = range;
    GetScene().ForEachComponent<Water>([&](Water& water) {
        if (const auto hit = water.Trace(origin, direction, distance)) {
            surface = &water;
            distance = *hit;
        }
    });
    if (!surface) {
        return;
    }
    const float falloff = 1.0f - distance / range;
    const float speed = helicopter->GetRotorSpeed();
    surface->Ripple(origin + direction * distance, speed * speed * falloff * falloff, scale);
}

void HelicopterDownwash::Serialize(Spot::Archive& archive) {
    archive.Field("range", range, {.min = 0.0f, .max = 100.0f, .tooltip = "Meters the rotor wash reaches"});
    archive.Field("interval", interval, {.min = 0.05f, .max = 5.0f, .tooltip = "Seconds between water ripples"});
    archive.Field("scale", scale, {.min = 0.1f, .max = 4.0f, .tooltip = "Of the water's authored ripple size"});
}

} // namespace Gameplay
