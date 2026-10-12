#include "player_controller.h"

#include "physics_gun.h"
#include "prop_carrier.h"
#include "weapon_holder.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(PlayerController);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

} // namespace

Spot::Quaternion PlayerController::GetViewRotation() const {
    return Spot::Quaternion::FromAxisAngle(Up, m_yaw) * Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, m_pitch);
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

void PlayerController::OnUpdate(float deltaTime) {
    namespace Input = Spot::Input;

    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    const float zoom = holder ? holder->GetAimZoom() : 1.0f;
    m_zoom += (zoom - m_zoom) * std::min(zoomSpeed * deltaTime * 60.0f, 1.0f);

    // A free cursor belongs to the rest of the screen, and the carrier and the gun take the mouse while turning props.
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    const bool turning = (carrier && carrier->IsTurning()) || (gun && gun->IsTurning());
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
    entity->GetTransform().SetRotation(GetViewRotation());
    if (auto* camera = entity->GetComponent<Spot::Camera>(); camera && m_fieldOfView > 0.0f) {
        // What a lens of that magnification sees.
        camera->SetFieldOfView(2.0f * std::atan(std::tan(m_fieldOfView * 0.5f) / m_zoom));
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
