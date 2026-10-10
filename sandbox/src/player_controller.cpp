#include "player_controller.h"

#include "physics_gun.h"
#include "prop_carrier.h"

#include <algorithm>

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

void PlayerController::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;

    // A free cursor belongs to the rest of the screen, and the carrier and the gun take the mouse while turning props.
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    const bool turning = (carrier && carrier->IsTurning()) || (gun && gun->IsTurning());
    if (Input::IsCursorLocked() && !turning) {
        const Spot::Vector2 mouseDelta = Input::GetMouseDelta();
        m_yaw -= mouseDelta.x * lookSensitivity;
        m_pitch = std::clamp(m_pitch - mouseDelta.y * lookSensitivity, -1.5f, 1.5f);
    }
    if (Input::IsKeyPressed(Spot::Key::F)) {
        ToggleFlashlight();
    }
}

void PlayerController::OnLateUpdate(float /*deltaTime*/) {
    if (Spot::Entity* entity = GetHead()) {
        entity->GetTransform().SetRotation(GetViewRotation());
    }
}

void PlayerController::Serialize(Spot::Archive& archive) {
    archive.Field("lookSensitivity", lookSensitivity,
                  {.min = 0.0f, .max = 0.1f, .speed = 0.0001f, .tooltip = "Radians per pixel the mouse moves"});
    archive.Field("head", head, {.tooltip = "Turns where the player looks, with the camera"});
    archive.Field("flashlight", flashlight, {.tooltip = "The light F switches"});
}

Spot::Entity* PlayerController::GetHead() const {
    return GetEntity().GetScene().GetEntity(head);
}
