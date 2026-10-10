#include "player_movement.h"

#include "player_controller.h"

#include <algorithm>

SPOT_COMPONENT(PlayerMovement);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

float GetAxis(Spot::Key positive, Spot::Key negative) {
    return static_cast<float>(Spot::Input::IsKeyDown(positive)) - static_cast<float>(Spot::Input::IsKeyDown(negative));
}

} // namespace

void PlayerMovement::SetFlying(bool flying) {
    m_flying = flying;
    m_verticalSpeed = 0.0f;
    // Flying goes through everything, so the character is put aside meanwhile, and made again where flying ended.
    if (auto* controller = GetEntity().GetComponent<Spot::CharacterController>()) {
        controller->SetEnabled(!flying);
    }
}

void PlayerMovement::OnStart() {
    m_spawnPoint = GetEntity().GetTransform().GetWorldPosition();
    if (const auto* controller = GetEntity().GetComponent<Spot::CharacterController>()) {
        m_standHeight = controller->GetHeight();
    }
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (const Spot::Entity* head = player ? GetScene().GetEntity(player->head) : nullptr) {
        m_headHeight = head->GetTransform().GetPosition().y;
    }
}

void PlayerMovement::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    // Read every frame, since a press can fall between two physics steps.
    m_jump = m_jump || Input::IsKeyPressed(Spot::Key::Space);
    m_crouchHeld = Input::IsKeyDown(Spot::Key::LeftControl);
    if (Input::IsKeyPressed(Spot::Key::V)) {
        SetFlying(!m_flying);
    }

    Spot::Transform& transform = GetEntity().GetTransform();
    if (transform.GetWorldPosition().y < fallHeight) {
        transform.SetPosition(m_spawnPoint);
        m_verticalSpeed = 0.0f;
    }
}

void PlayerMovement::OnFixedUpdate(float fixedDeltaTime) {
    if (m_flying) {
        Fly(fixedDeltaTime);
    } else if (auto* controller = GetEntity().GetComponent<Spot::CharacterController>()) {
        Walk(*controller, fixedDeltaTime);
    }
    m_jump = false;
}

void PlayerMovement::OnLateUpdate(float deltaTime) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    Spot::Entity* head = player ? GetScene().GetEntity(player->head) : nullptr;
    if (!head) {
        return;
    }
    const float target = m_crouching ? m_standHeight - crouchHeight : 0.0f;
    m_headDrop += (target - m_headDrop) * std::min(deltaTime * 12.0f, 1.0f);
    Spot::Vector3 position = head->GetTransform().GetPosition();
    position.y = m_headHeight - m_headDrop;
    head->GetTransform().SetPosition(position);
}

void PlayerMovement::Walk(Spot::CharacterController& controller, float deltaTime) {
    using Spot::Key;
    if (m_crouchHeld != m_crouching && (m_crouchHeld || HasRoomToStand(controller))) {
        SetCrouching(controller, m_crouchHeld);
    }

    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto yaw = Spot::Quaternion::FromAxisAngle(Up, player ? player->GetYaw() : 0.0f);
    const Spot::Vector3 direction =
        Spot::Normalize(yaw * Spot::Vector3{GetAxis(Key::D, Key::A), 0.0f, -GetAxis(Key::W, Key::S)});
    const float speed = m_crouching ? crouchSpeed : Spot::Input::IsKeyDown(Key::LeftShift) ? runSpeed : walkSpeed;

    if (controller.IsGrounded()) {
        // On the ground, the controller keeps the player on it down slopes and steps.
        m_verticalSpeed = m_jump && !m_crouching ? jumpSpeed : 0.0f;
    } else {
        m_verticalSpeed += GetScene().GetPhysics().GetGravity().y * deltaTime;
    }
    controller.Move(direction * speed + Spot::Vector3{0.0f, m_verticalSpeed, 0.0f}, deltaTime);
    // A ceiling stops a jump.
    m_verticalSpeed = std::min(m_verticalSpeed, controller.GetVelocity().y);
}

void PlayerMovement::Fly(float deltaTime) {
    using Spot::Key;
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const Spot::Quaternion view = player ? player->GetViewRotation() : Spot::Quaternion{};
    const Spot::Vector3 move = view * Spot::Vector3{GetAxis(Key::D, Key::A), 0.0f, -GetAxis(Key::W, Key::S)} +
                               Up * GetAxis(Key::Space, Key::LeftControl);
    const float speed = Spot::Input::IsKeyDown(Key::LeftShift) ? fastFlySpeed : flySpeed;
    Spot::Transform& transform = GetEntity().GetTransform();
    transform.SetPosition(transform.GetPosition() + Spot::Normalize(move) * (speed * deltaTime));
}

void PlayerMovement::SetCrouching(Spot::CharacterController& controller, bool crouching) {
    m_crouching = crouching;
    // The capsule shrinks down to the feet, which stay where they are.
    controller.SetHeight(crouching ? crouchHeight : m_standHeight);
    controller.SetCenter({0.0f, crouching ? (crouchHeight - m_standHeight) * 0.5f : 0.0f, 0.0f});
}

bool PlayerMovement::HasRoomToStand(const Spot::CharacterController& controller) const {
    const float radius = controller.GetRadius();
    const Spot::Vector3 top = GetEntity().GetTransform().GetWorldPosition() + controller.GetCenter() +
                              Up * (controller.GetHeight() * 0.5f - radius);
    const auto hit = GetScene().GetPhysics().SphereCast(top, radius * 0.9f, Up, m_standHeight - crouchHeight,
                                                        {.ignore = GetEntity().GetId()});
    return !hit.has_value();
}

void PlayerMovement::Serialize(Spot::Archive& archive) {
    archive.Field("walkSpeed", walkSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("runSpeed", runSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("crouchSpeed", crouchSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("jumpSpeed", jumpSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("flySpeed", flySpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.1f});
    archive.Field("fastFlySpeed", fastFlySpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.1f});
    archive.Field("crouchHeight", crouchHeight,
                  {.min = 0.1f, .max = 10.0f, .speed = 0.01f, .tooltip = "The capsule's height while crouching"});
    archive.Field("fallHeight", fallHeight, {.tooltip = "Below this height, the player goes back to the start"});
}
