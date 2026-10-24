#include "player_movement.h"
#include <spot/scene/audio/audio_system.h>
#include <spot/scene/physics/physics_world.h>

#include "physics_gun.h"
#include "player_controller.h"
#include "player_health.h"
#include "prop_carrier.h"
#include "sound_bank.h"
#include "weapon_holder.h"

#include <algorithm>
#include <cmath>

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

void PlayerMovement::Push(Spot::Vector3 velocity) {
    m_push += Spot::Vector3{velocity.x, 0.0f, velocity.z};
    m_lift = std::max(m_lift, velocity.y);
}

void PlayerMovement::Respawn() {
    GetEntity().GetTransform().SetPosition(m_spawnPoint);
    m_verticalSpeed = 0.0f;
    m_push = {};
    m_lift = 0.0f;
    m_sprinting = false;
}

bool PlayerMovement::CanSprint() const {
    const Spot::Entity& player = GetEntity();
    const auto* health = player.GetComponent<PlayerHealth>();
    const auto* holder = player.GetComponent<WeaponHolder>();
    const auto* carrier = player.GetComponent<PropCarrier>();
    const auto* gun = player.GetComponent<PhysicsGun>();
    return !(health && health->IsDead()) && !(holder && holder->IsAiming()) && !(carrier && carrier->IsHolding()) &&
           !(gun && gun->IsGrabbing());
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
    m_jump = m_jump || Input::IsActionPressed("Jump");
    m_crouchHeld = Input::IsActionDown("Crouch");
    if (Input::IsActionPressed("Noclip")) {
        SetFlying(!m_flying);
    }

    Spot::Transform& transform = GetEntity().GetTransform();
    if (transform.GetWorldPosition().y < fallHeight) {
        Respawn();
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
    Stride(deltaTime);
    const auto* player = GetEntity().GetComponent<PlayerController>();
    Spot::Entity* head = player ? GetScene().GetEntity(player->head) : nullptr;
    if (!head) {
        return;
    }
    // The dead sink down to the ground.
    const auto* health = GetEntity().GetComponent<PlayerHealth>();
    const float target = health && health->IsDead() ? m_headHeight + m_standHeight * 0.5f - 0.35f
                         : m_crouching              ? m_standHeight - crouchHeight
                                                    : 0.0f;
    m_headDrop += (target - m_headDrop) * std::min(deltaTime * 12.0f, 1.0f);
    // Landing compresses the legs: the head dips and springs back, damped so it settles without bouncing.
    constexpr float Stiffness = 120.0f;
    const float damping = 2.0f * std::sqrt(Stiffness) * 0.85f;
    for (float left = deltaTime; left > 0.0f; left -= 1.0f / 120.0f) {
        const float step = std::min(left, 1.0f / 120.0f);
        m_dipVelocity += (-Stiffness * m_dip - damping * m_dipVelocity) * step;
        m_dip += m_dipVelocity * step;
    }
    // Lowest as each foot lands, highest between steps.
    m_bob = -bobHeight * m_bobWeight * std::cos(2.0f * Spot::Pi * m_stepPhase);
    Spot::Vector3 position = head->GetTransform().GetPosition();
    position.y = m_headHeight - m_headDrop + m_bob + m_dip;
    head->GetTransform().SetPosition(position);
}

void PlayerMovement::Walk(Spot::CharacterController& controller, float deltaTime) {
    if (m_crouchHeld != m_crouching && (m_crouchHeld || HasRoomToStand(controller))) {
        SetCrouching(controller, m_crouchHeld);
    }

    const auto* player = GetEntity().GetComponent<PlayerController>();
    auto* health = GetEntity().GetComponent<PlayerHealth>();
    const bool alive = !(health && health->IsDead());
    const auto yaw = Spot::Quaternion::FromAxisAngle(Up, player ? player->GetYaw() : 0.0f);
    const Spot::Vector3 move = alive ? GetMove() : Spot::Vector3{};
    const Spot::Vector3 direction = yaw * move;
    // Forward, or nearly, and once in the air only as the jump began.
    const bool sprint = Spot::Input::IsActionDown("Sprint") && !m_crouching && -move.z > 0.5f && CanSprint();
    m_sprinting = sprint && (controller.IsGrounded() || m_sprinting);
    const float speed = m_crouching ? crouchSpeed : m_sprinting ? runSpeed : walkSpeed;

    if (m_lift > 0.0f) {
        m_verticalSpeed = std::max(m_verticalSpeed, m_lift);
        m_lift = 0.0f;
    } else if (controller.IsGrounded()) {
        // On the ground, the controller keeps the player on it down slopes and steps.
        m_verticalSpeed = m_jump && !m_crouching && alive ? jumpSpeed : 0.0f;
    } else {
        m_verticalSpeed += GetScene().GetOrAddSystem<Spot::PhysicsWorld>().GetGravity().y * deltaTime;
    }
    const float falling = -m_verticalSpeed;
    controller.Move(direction * speed + m_push + Spot::Vector3{0.0f, m_verticalSpeed, 0.0f}, deltaTime);
    // A push slides to a stop on the ground, and barely slows in the air.
    m_push = m_push * std::exp(-(controller.IsGrounded() ? 6.0f : 0.3f) * deltaTime);
    if (controller.IsGrounded() && !m_wasGrounded) {
        if (falling > 3.0f) {
            m_landingSpeed = falling;
        }
        if (health) {
            health->Land(falling);
        }
    }
    m_wasGrounded = controller.IsGrounded();
    // A ceiling stops a jump.
    m_verticalSpeed = std::min(m_verticalSpeed, controller.GetVelocity().y);
}

void PlayerMovement::Fly(float deltaTime) {
    m_sprinting = false;
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
    const auto hit = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().SphereCast(
        top, radius * 0.9f, Up, m_standHeight - crouchHeight, {.ignore = GetEntity().GetId()});
    return !hit.has_value();
}

void PlayerMovement::Stride(float deltaTime) {
    const auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
    const bool grounded = !m_flying && controller && controller->IsEnabled() && controller->IsGrounded();
    float speed = 0.0f;
    if (grounded) {
        const Spot::Vector3 velocity = controller->GetVelocity();
        speed = Spot::Length(Spot::Vector3{velocity.x, 0.0f, velocity.z});
    }
    if (m_landingSpeed > 0.0f) {
        m_dipVelocity -= std::min(m_landingSpeed * 0.06f, 0.8f);
        PlayFootstep(std::clamp(m_landingSpeed / 8.0f, 0.6f, 1.6f));
        m_landingSpeed = 0.0f;
    }
    const float weight = speed > 0.3f ? std::min(speed / std::max(walkSpeed, 0.1f), 1.4f) : 0.0f;
    m_bobWeight += (weight - m_bobWeight) * (1.0f - std::exp(-8.0f * deltaTime));
    if (speed <= 0.3f) {
        // Standing still, the next walk starts mid-stride, so its first step lands half a stride away.
        if (m_bobWeight < 0.01f) {
            m_stepPhase = std::floor(m_stepPhase) + 0.5f;
        }
        return;
    }
    const float stride = strideLength * std::sqrt(speed / std::max(walkSpeed, 0.1f));
    const float before = m_stepPhase;
    m_stepPhase += speed * deltaTime / std::max(stride, 0.1f);
    // Kept small, so it stays precise however long the player walks.
    if (m_stepPhase > 1024.0f) {
        m_stepPhase -= 1024.0f;
        return;
    }
    if (std::floor(m_stepPhase) > std::floor(before)) {
        PlayFootstep(m_crouching ? 0.45f : speed > walkSpeed * 1.2f ? 1.4f : 1.0f);
    }
}

void PlayerMovement::PlayFootstep(float loudness) {
    const auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
    if (!controller) {
        return;
    }
    const Spot::Vector3 feet = GetEntity().GetTransform().GetWorldPosition() + controller->GetCenter() -
                               Spot::Vector3{0.0f, controller->GetHeight() * 0.5f, 0.0f};
    const auto ground = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(
        feet + Spot::Vector3{0.0f, 0.2f, 0.0f}, {0.0f, -1.0f, 0.0f}, 0.6f, {.ignore = GetEntity().GetId()});
    const SoundBank* bank = SoundBank::Find(GetScene());
    const SurfaceSounds* sounds =
        bank ? bank->GetSurface(Surface::Of(ground ? GetScene().GetEntity(ground->entity) : nullptr)) : nullptr;
    if (!sounds || sounds->footsteps.empty()) {
        return;
    }
    // A different variant each time, never the same twice in a row, at a slightly different pitch and level.
    const auto next = [this] {
        m_random ^= m_random << 13;
        m_random ^= m_random >> 17;
        m_random ^= m_random << 5;
        return static_cast<float>(m_random >> 8) / 16777216.0f;
    };
    const std::size_t count = sounds->footsteps.size();
    if (count > 1) {
        m_footstep += 1 + static_cast<std::size_t>(next() * static_cast<float>(count - 1));
    }
    const Spot::AssetRef<Spot::Sound>& sound = sounds->footsteps[m_footstep % count];
    GetScene().GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
        sound.GetShared(), feet,
        {.volume = footstepVolume * loudness * (0.85f + next() * 0.15f),
         .pitch = 0.93f + next() * 0.14f,
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
    archive.Field("strideLength", strideLength,
                  {.min = 0.1f, .max = 10.0f, .speed = 0.01f, .tooltip = "Meters between footsteps when walking"});
    archive.Field("footstepVolume", footstepVolume, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("bobHeight", bobHeight, {.min = 0.0f, .max = 0.2f, .speed = 0.001f});
    archive.Field("fallHeight", fallHeight, {.tooltip = "Below this height, the player goes back to the start"});
}
