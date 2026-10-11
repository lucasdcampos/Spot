#include "player_movement.h"

#include "player_controller.h"
#include "sound_bank.h"

#include <algorithm>

SPOT_COMPONENT(PlayerMovement);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

float GetAxis(std::string_view positive, std::string_view negative) {
    return static_cast<float>(Spot::Input::IsActionDown(positive)) -
           static_cast<float>(Spot::Input::IsActionDown(negative));
}

// Where Move goes, in the player's space: -Z is forward. A stick pushed part of the way goes slower.
Spot::Vector3 GetMove() {
    const Spot::Vector2 move = Spot::Input::GetActionVector2("Move");
    const float length = Spot::Length(move);
    return length > 1.0f ? Spot::Vector3{move.x, 0.0f, -move.y} / length : Spot::Vector3{move.x, 0.0f, -move.y};
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
    StartCoroutine(BobCamera());
}

bool PlayerMovement::IsWalking() const {
    const auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
    return !m_flying && !m_crouching && controller && controller->IsGrounded() && Spot::Input::IsCursorLocked() &&
           Spot::LengthSquared(GetMove()) > 0.01f;
}

Spot::Task PlayerMovement::BobCamera() {
    while (true) {
        co_await Spot::WaitUntil{[this] { return IsWalking(); }};
        auto& tweens = Spot::TweenSystem::Get(GetScene());
        const float third = std::max(bobPeriod, 0.03f) / 3.0f;
        const auto set = [this](float value) { m_bob = value; };
        const auto id = tweens.Play(
            *this, Spot::Tween::Sequence({Spot::Tween::Value(third, m_bob, bobHeight, set, Spot::Ease::SineInOut),
                                          Spot::Tween::Value(third, bobHeight, -bobHeight, set, Spot::Ease::SineInOut),
                                          Spot::Tween::Value(third, -bobHeight, 0.0f, set, Spot::Ease::SineInOut)}));
        co_await Spot::WaitUntil{
            [this, id] { return !IsWalking() || !Spot::TweenSystem::Get(GetScene()).IsPlaying(id); }};
        tweens.Cancel(id);
        tweens.Play(*this, Spot::Tween::Value(0.1f, m_bob, 0.0f, set, Spot::Ease::QuadOut));
        co_await Spot::WaitForSeconds{0.1f};
    }
}

void PlayerMovement::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    // Read every frame, since a press can fall between two physics steps.
    m_jump = m_jump || Input::IsActionPressed("Jump");
    m_crouchHeld = Input::IsActionDown("Crouch");
    if (Input::IsActionPressed("Noclip")) {
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
    position.y = m_headHeight - m_headDrop + m_bob;
    head->GetTransform().SetPosition(position);
}

void PlayerMovement::Walk(Spot::CharacterController& controller, float deltaTime) {
    if (m_crouchHeld != m_crouching && (m_crouchHeld || HasRoomToStand(controller))) {
        SetCrouching(controller, m_crouchHeld);
    }

    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto yaw = Spot::Quaternion::FromAxisAngle(Up, player ? player->GetYaw() : 0.0f);
    const Spot::Vector3 direction = yaw * GetMove();
    const float speed = m_crouching ? crouchSpeed : Spot::Input::IsActionDown("Sprint") ? runSpeed : walkSpeed;

    if (controller.IsGrounded()) {
        // On the ground, the controller keeps the player on it down slopes and steps.
        m_verticalSpeed = m_jump && !m_crouching ? jumpSpeed : 0.0f;
    } else {
        m_verticalSpeed += GetScene().GetPhysics().GetGravity().y * deltaTime;
    }
    controller.Move(direction * speed + Spot::Vector3{0.0f, m_verticalSpeed, 0.0f}, deltaTime);
    if (controller.IsGrounded()) {
        const Spot::Vector3 velocity = controller.GetVelocity();
        Step(controller, Spot::Length(Spot::Vector3{velocity.x, 0.0f, velocity.z}) * deltaTime);
    }
    // A ceiling stops a jump.
    m_verticalSpeed = std::min(m_verticalSpeed, controller.GetVelocity().y);
}

void PlayerMovement::Fly(float deltaTime) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const Spot::Quaternion view = player ? player->GetViewRotation() : Spot::Quaternion{};
    Spot::Vector3 move = view * GetMove() + Up * GetAxis("Jump", "Crouch");
    if (Spot::LengthSquared(move) > 1.0f) {
        move = Spot::Normalize(move);
    }
    const float speed = Spot::Input::IsActionDown("Sprint") ? fastFlySpeed : flySpeed;
    Spot::Transform& transform = GetEntity().GetTransform();
    transform.SetPosition(transform.GetPosition() + move * (speed * deltaTime));
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

void PlayerMovement::Step(const Spot::CharacterController& controller, float distance) {
    m_stride += distance;
    if (m_stride < strideLength) {
        return;
    }
    m_stride = 0.0f;
    const Spot::Vector3 feet = GetEntity().GetTransform().GetWorldPosition() + controller.GetCenter() -
                               Spot::Vector3{0.0f, controller.GetHeight() * 0.5f, 0.0f};
    const auto ground = GetScene().GetPhysics().Raycast(feet + Spot::Vector3{0.0f, 0.2f, 0.0f}, {0.0f, -1.0f, 0.0f},
                                                        0.6f, {.ignore = GetEntity().GetId()});
    const SoundBank* bank = SoundBank::Find(GetScene());
    const SurfaceSounds* sounds =
        bank ? bank->GetSurface(Surface::Of(ground ? GetScene().GetEntity(ground->entity) : nullptr)) : nullptr;
    if (!sounds || sounds->footsteps.empty()) {
        return;
    }
    const Spot::AssetRef<Spot::Sound>& sound = sounds->footsteps[m_footstep++ % sounds->footsteps.size()];
    const float loudness = m_crouching ? 0.4f : Spot::Input::IsActionDown("Sprint") ? 1.2f : 1.0f;
    GetScene().GetAudio().PlayOneShot(sound.GetShared(), feet,
                                      {.volume = footstepVolume * loudness,
                                       .bus = "Effects",
                                       .spatialSettings = {.minDistance = 2.0f, .maxDistance = 25.0f},
                                       .maxInstances = 3});
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
    archive.Field("strideLength", strideLength, {.min = 0.1f, .max = 10.0f, .speed = 0.01f});
    archive.Field("footstepVolume", footstepVolume, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("bobHeight", bobHeight, {.min = 0.0f, .max = 0.2f, .speed = 0.001f});
    archive.Field("bobPeriod", bobPeriod, {.min = 0.03f, .max = 2.0f, .speed = 0.01f});
    archive.Field("fallHeight", fallHeight, {.tooltip = "Below this height, the player goes back to the start"});
}
