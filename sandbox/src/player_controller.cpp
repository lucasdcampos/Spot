#include "player_controller.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(PlayerController);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

} // namespace

Spot::Quaternion PlayerController::GetViewRotation() const {
    return Spot::Quaternion::FromAxisAngle(Up, m_yaw) * Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, m_pitch);
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

void PlayerController::OnStart() {
    m_spawnPoint = GetEntity().GetTransform().GetWorldPosition();
}

void PlayerController::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;

    // A free cursor belongs to the rest of the screen.
    if (Input::IsCursorLocked()) {
        const Spot::Vector2 mouseDelta = Input::GetMouseDelta();
        m_yaw -= mouseDelta.x * lookSensitivity;
        m_pitch = std::clamp(m_pitch - mouseDelta.y * lookSensitivity, -1.5f, 1.5f);
        if (Input::IsMouseButtonPressed(Spot::MouseButton::Left)) {
            Shoot();
        }
    }
    if (Input::IsKeyPressed(Spot::Key::F)) {
        ToggleFlashlight();
    }
    // Read every frame, since a press can fall between two physics steps.
    m_jump = m_jump || Input::IsKeyPressed(Spot::Key::Space);

    // Back to the start after falling off the world.
    Spot::Transform& transform = GetEntity().GetTransform();
    if (transform.GetWorldPosition().y < fallHeight) {
        transform.SetPosition(m_spawnPoint);
        m_verticalSpeed = 0.0f;
    }
}

void PlayerController::OnLateUpdate(float /*deltaTime*/) {
    if (Spot::Entity* entity = GetHead()) {
        entity->GetTransform().SetRotation(GetViewRotation());
    }
}

void PlayerController::OnFixedUpdate(float fixedDeltaTime) {
    namespace Input = Spot::Input;
    using Spot::Key;

    auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
    if (!controller) {
        return;
    }

    const auto yaw = Spot::Quaternion::FromAxisAngle(Up, m_yaw);
    const float forward = static_cast<float>(Input::IsKeyDown(Key::W)) - static_cast<float>(Input::IsKeyDown(Key::S));
    const float right = static_cast<float>(Input::IsKeyDown(Key::D)) - static_cast<float>(Input::IsKeyDown(Key::A));
    const Spot::Vector3 direction = Spot::Normalize(yaw * Spot::Vector3{right, 0.0f, -forward});
    const float speed = Input::IsKeyDown(Key::LeftShift) ? runSpeed : walkSpeed;

    if (controller->IsGrounded()) {
        // On the ground, the controller keeps the player on it down slopes and steps.
        m_verticalSpeed = m_jump ? jumpSpeed : 0.0f;
    } else {
        m_verticalSpeed += GetScene().GetPhysics().GetGravity().y * fixedDeltaTime;
    }
    m_jump = false;

    controller->Move(direction * speed + Spot::Vector3{0.0f, m_verticalSpeed, 0.0f}, fixedDeltaTime);
    // A ceiling stops a jump.
    m_verticalSpeed = std::min(m_verticalSpeed, controller->GetVelocity().y);
}

void PlayerController::Serialize(Spot::Archive& archive) {
    archive.Field("walkSpeed", walkSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("runSpeed", runSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("jumpSpeed", jumpSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("lookSensitivity", lookSensitivity,
                  {.min = 0.0f, .max = 0.1f, .speed = 0.0001f, .tooltip = "Radians per pixel the mouse moves"});
    archive.Field("head", head, {.tooltip = "Turns where the player looks, with the camera"});
    archive.Field("flashlight", flashlight, {.tooltip = "The light F switches"});
    archive.Field("ballMaterial", ballMaterial);
    archive.Field("ballSpeed", ballSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.1f});
    archive.Field("fallHeight", fallHeight, {.tooltip = "Below this height, the player goes back to the start"});
}

Spot::Entity* PlayerController::GetHead() const {
    return GetEntity().GetScene().GetEntity(head);
}
