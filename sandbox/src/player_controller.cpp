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

void PlayerController::Shoot() {
    Spot::Entity* eye = GetHead();
    if (!eye) {
        return;
    }
    const Spot::Vector3 forward = GetViewRotation() * Spot::Vector3{0.0f, 0.0f, -1.0f};
    constexpr float Diameter = 0.4f;

    auto& ball = GetEntity().GetScene().CreateEntity("Ball");
    ball.GetTransform().SetPosition(eye->GetTransform().GetWorldPosition() + forward * 0.9f);
    ball.GetTransform().SetScale({Diameter, Diameter, Diameter});
    ball.AddComponent<Spot::MeshRenderer>(Spot::Assets::Load<Spot::Mesh>(Spot::BuiltInAssets::SphereMesh),
                                          ballMaterial);
    ball.AddComponent<Spot::SphereCollider>().SetRestitution(0.4f);
    ball.AddComponent<Spot::Rigidbody>().SetMass(3.0f);
    ball.GetComponent<Spot::Rigidbody>()->SetLinearVelocity(forward * ballSpeed);
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

    if (Input::IsKeyPressed(Spot::Key::Digit1)) {
        activeTool = Tool::Launcher;
    }
    if (Input::IsKeyPressed(Spot::Key::Digit2)) {
        activeTool = Tool::PhysicsGun;
    }
    // A free cursor belongs to the rest of the screen, and the carrier and the gun take the mouse while turning props.
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    const bool turning = (carrier && carrier->IsTurning()) || (gun && gun->IsTurning());
    if (Input::IsCursorLocked() && !turning) {
        const Spot::Vector2 mouseDelta = Input::GetMouseDelta();
        m_yaw -= mouseDelta.x * lookSensitivity;
        m_pitch = std::clamp(m_pitch - mouseDelta.y * lookSensitivity, -1.5f, 1.5f);
    }
    // A carried prop takes the click, to be thrown.
    const bool carrying = carrier && carrier->IsHolding();
    if (Input::IsCursorLocked() && activeTool == Tool::Launcher && !carrying &&
        Input::IsMouseButtonPressed(Spot::MouseButton::Left)) {
        Shoot();
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
    archive.Field("ballMaterial", ballMaterial);
    archive.Field("ballSpeed", ballSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.1f});
}

Spot::Entity* PlayerController::GetHead() const {
    return GetEntity().GetScene().GetEntity(head);
}
