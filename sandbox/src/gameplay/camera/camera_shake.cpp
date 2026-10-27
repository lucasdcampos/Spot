#include "gameplay/camera/camera_shake.h"

#include "gameplay/core/pawn.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(CameraShake);

void CameraShake::Add(Spot::Scene& scene, Spot::Vector3 center, float strength, float radius) {
    if (!(radius > 0.0f)) {
        return;
    }
    scene.ForEachComponent<CameraShake>([&](CameraShake& shake) {
        const Pawn* pawn = Pawn::Of(shake.GetEntity());
        const Spot::Vector3 view =
            pawn ? pawn->GetViewPoint().position : shake.GetEntity().GetTransform().GetWorldPosition();
        const float closeness = 1.0f - Spot::Distance(view, center) / radius;
        if (closeness > 0.0f) {
            shake.Add(strength * closeness * closeness, 0.4f + 0.5f * closeness);
        }
    });
}

void CameraShake::Add(float radians, float seconds) {
    if (!std::isfinite(radians) || radians <= 0.0f || !std::isfinite(seconds) || seconds <= 0.0f) {
        return;
    }
    const float current = m_length > 0.0f ? m_strength * m_left / m_length : 0.0f;
    if (radians >= current) {
        m_strength = std::min(radians, maxAngle);
        m_length = m_left = seconds;
    }
}

Spot::Quaternion CameraShake::GetRotation() const {
    if (m_left <= 0.0f || m_length <= 0.0f) {
        return {};
    }
    // Dying down, quickly at first; waves out of step on each axis, so it jolts rather than sways.
    const float left = m_left / m_length;
    const float amount = m_strength * left * left;
    const float t = m_clock;
    const float pitch = amount * (0.6f * std::sin(t * 41.0f) + 0.4f * std::sin(t * 23.0f + 1.7f));
    const float yaw = amount * (0.6f * std::sin(t * 37.0f + 0.5f) + 0.4f * std::sin(t * 19.0f + 2.9f));
    const float roll = amount * 0.5f * std::sin(t * 31.0f + 4.1f);
    return Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, pitch) *
           Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, yaw) *
           Spot::Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, roll);
}

void CameraShake::OnUpdate(float deltaTime) {
    m_left = std::max(m_left - deltaTime, 0.0f);
    m_clock += deltaTime;
}

void CameraShake::OnDisable() {
    m_left = 0.0f;
}

void CameraShake::Serialize(Spot::Archive& archive) {
    archive.Field("maxAngle", maxAngle,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .tooltip = "Radians it turns the view by at most"});
}

} // namespace Gameplay
