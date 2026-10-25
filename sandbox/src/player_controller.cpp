#include "player_controller.h"

#include "physics_gun.h"
#include "player_movement.h"
#include "prop_carrier.h"
#include "weapon_holder.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(PlayerController);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

} // namespace

Spot::Quaternion PlayerController::GetViewRotation() const {
    return Spot::Quaternion::FromAxisAngle(Up, m_yaw) *
           Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, std::clamp(m_pitch + m_recoil, -1.5f, 1.5f));
}

void PlayerController::SetLook(float yaw, float pitch) {
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -1.5f, 1.5f);
}

Spot::Vector3 PlayerController::GetEyePosition() const {
    const Spot::Entity* eye = GetHead();
    return (eye ? eye : &GetEntity())->GetTransform().GetWorldPosition();
}

Spot::Vector3 PlayerController::GetAimDirection() const {
    return GetViewRotation() * Spot::Vector3{0.0f, 0.0f, -1.0f};
}

void PlayerController::ToggleFlashlight() {
    if (Spot::Entity* entity = GetEntity().GetScene().GetEntity(flashlight)) {
        if (auto* light = entity->GetComponent<Spot::Light>()) {
            light->SetEnabled(!light->IsEnabled());
        }
    }
}

float PlayerController::GetFieldOfView() const {
    if (m_fieldOfView > 0)
        return m_fieldOfView;
    const auto* entity = GetHead();
    const auto* camera = entity ? entity->GetComponent<Spot::Camera>() : nullptr;
    return camera ? camera->GetFieldOfView() : Spot::ToRadians(60.0f);
}
void PlayerController::SetFieldOfView(float radians) {
    m_fieldOfView = std::clamp(std::isfinite(radians) ? radians : Spot::ToRadians(60.0f), Spot::ToRadians(30.0f),
                               Spot::ToRadians(130.0f));
    if (auto* entity = GetHead()) {
        if (auto* camera = entity->GetComponent<Spot::Camera>())
            camera->SetFieldOfView(2.0f * std::atan(std::tan(m_fieldOfView * 0.5f) / m_zoom));
    }
}

void PlayerController::OnStart() {
    if (const Spot::Entity* entity = GetHead()) {
        if (const auto* camera = entity->GetComponent<Spot::Camera>()) {
            m_fieldOfView = camera->GetFieldOfView();
        }
    }
}

void PlayerController::Kick(float radians, float recoveryTime) {
    auto& tweens = Spot::TweenSystem::Get(GetScene());
    tweens.Cancel(m_recoilTween);
    m_recoil = std::clamp(m_recoil + radians, 0.0f, 0.2f);
    m_recoilTween = tweens.Play(*this, Spot::Tween::Value(
                                           std::max(recoveryTime, 0.01f), m_recoil, 0.0f,
                                           [this](float value) { m_recoil = value; }, Spot::Ease::QuadOut));
}

void PlayerController::Shake(float radians, float seconds) {
    if (!std::isfinite(radians) || radians <= 0.0f || !std::isfinite(seconds) || seconds <= 0.0f) {
        return;
    }
    // A stronger shake takes over a weaker one still going on.
    const float current = m_shakeLength > 0.0f ? m_shake * m_shakeLeft / m_shakeLength : 0.0f;
    if (radians >= current) {
        m_shake = std::min(radians, 0.2f);
        m_shakeLength = m_shakeLeft = seconds;
    }
}

void PlayerController::OnDisable() {
    if (auto* tweens = GetScene().GetSystem<Spot::TweenSystem>()) {
        tweens->Cancel(m_recoilTween);
    }
    m_recoil = 0;
    m_shakeLeft = 0;
}

void PlayerController::OnUpdate(float deltaTime) {
    namespace Input = Spot::Input;

    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    const float zoom = holder ? holder->GetAimZoom() : 1.0f;
    m_zoom += (zoom - m_zoom) * std::min(zoomSpeed * deltaTime * 60.0f, 1.0f);
    const auto* movement = GetEntity().GetComponent<PlayerMovement>();
    const float run = movement && movement->IsSprinting() ? 1.0f : 0.0f;
    m_run += (run - m_run) * (1.0f - std::exp(-6.0f * deltaTime));
    m_shakeLeft = std::max(m_shakeLeft - deltaTime, 0.0f);
    m_shakeClock += deltaTime;

    // A free cursor belongs to the rest of the screen, the carrier and the gun take the mouse while turning props, and
    // the weapon wheel while it is open.
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    const bool turning =
        (carrier && carrier->IsTurning()) || (gun && gun->IsTurning()) || (holder && holder->IsWheelOpen());
    if (Input::IsCursorLocked() && !turning) {
        // Up is positive, so the pitch rises with it.
        const Spot::Vector2 look = Input::GetActionVector2("Look") * (lookSensitivity / m_zoom);
        m_yaw -= look.x;
        m_pitch = std::clamp(m_pitch + look.y, -1.5f, 1.5f);
    }
    if (Input::IsActionPressed("Flashlight")) {
        ToggleFlashlight();
    }
}

void PlayerController::OnLateUpdate(float /*deltaTime*/) {
    Spot::Entity* entity = GetHead();
    if (!entity) {
        return;
    }
    Spot::Quaternion rotation = GetViewRotation();
    if (m_shakeLeft > 0.0f && m_shakeLength > 0.0f) {
        // Dying down, quickly at first; waves out of step on each axis, so it jolts rather than sways.
        const float left = m_shakeLeft / m_shakeLength;
        const float amount = m_shake * left * left;
        const float t = m_shakeClock;
        const float pitch = amount * (0.6f * std::sin(t * 41.0f) + 0.4f * std::sin(t * 23.0f + 1.7f));
        const float yaw = amount * (0.6f * std::sin(t * 37.0f + 0.5f) + 0.4f * std::sin(t * 19.0f + 2.9f));
        const float roll = amount * 0.5f * std::sin(t * 31.0f + 4.1f);
        rotation = rotation * Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, pitch) *
                   Spot::Quaternion::FromAxisAngle(Up, yaw) * Spot::Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, roll);
    }
    entity->GetTransform().SetRotation(rotation);
    if (auto* camera = entity->GetComponent<Spot::Camera>(); camera && m_fieldOfView > 0.0f) {
        // What a lens of that magnification sees, a little wider running.
        camera->SetFieldOfView(2.0f * std::atan(std::tan(m_fieldOfView * 0.5f) * (1.0f + 0.07f * m_run) / m_zoom));
    }
}

void PlayerController::Serialize(Spot::Archive& archive) {
    archive.Field("lookSensitivity", lookSensitivity,
                  {.min = 0.0f, .max = 0.1f, .speed = 0.0001f, .tooltip = "Radians per pixel the mouse moves"});
    archive.Field("zoomSpeed", zoomSpeed, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("head", head, {.tooltip = "Turns where the player looks, with the camera"});
    archive.Field("flashlight", flashlight, {.tooltip = "The light the Flashlight action switches"});
}

Spot::Entity* PlayerController::GetHead() const {
    return GetEntity().GetScene().GetEntity(head);
}
