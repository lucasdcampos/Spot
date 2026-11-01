#include "gameplay/character/character_movement.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/character/character.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/physics_drive.h"
#include "gameplay/core/player_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Gameplay {

SPOT_COMPONENT(CharacterMovement);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

// To the machine moving a character: a push the server gave it, as a blast's.
struct CharacterPushed {
    static constexpr std::string_view NetName = "Gameplay.CharacterPushed";
    Spot::Vector3 velocity;

    void Serialize(Spot::Archive& archive) { archive.Field("velocity", velocity); }
};

// To the server, from the machine moving a character: it landed this fast, for the damage of the fall.
struct CharacterLanded {
    static constexpr std::string_view NetName = "Gameplay.CharacterLanded";
    float speed = 0.0f;

    void Serialize(Spot::Archive& archive) { archive.Field("speed", speed); }
};

} // namespace

// Hears what the server and the machines moving characters tell each other.
class CharacterNetwork final : public Spot::SceneSystem {
protected:
    void OnUpdate(float /*deltaTime*/) override {
        Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
        if (!system || system == m_bound) {
            return;
        }
        m_bound = system;
        if (system->IsClient()) {
            system->OnObjectMessage<CharacterPushed>(
                [](Spot::NetworkObject& object, Spot::NetPeerId, CharacterPushed& pushed) {
                    auto* movement = object.GetEntity().GetComponent<CharacterMovement>();
                    if (movement && object.HasAuthority() && std::isfinite(pushed.velocity.x) &&
                        std::isfinite(pushed.velocity.y) && std::isfinite(pushed.velocity.z)) {
                        movement->Push(pushed.velocity);
                    }
                });
        } else {
            system->OnObjectMessage<CharacterLanded>(
                [](Spot::NetworkObject& object, Spot::NetPeerId from, CharacterLanded& landed) {
                    auto* movement = object.GetEntity().GetComponent<CharacterMovement>();
                    if (movement && object.GetOwner() == from && object.IsDecidedByClient() &&
                        std::isfinite(landed.speed)) {
                        movement->Landed(landed.speed);
                    }
                });
        }
    }

private:
    const Spot::NetworkSystem* m_bound = nullptr;
};

std::vector<std::string> CharacterControls::GetActions() const {
    return {move, jump, crouch, sprint, noclip};
}

CharacterIntent CharacterControls::Read(const InputContext& input) const {
    return {.move = input.GetVector2(move),
            .jump = input.IsDown(jump),
            .crouch = input.IsDown(crouch),
            .sprint = input.IsDown(sprint),
            .noclip = input.IsDown(noclip)};
}

void CharacterControls::Serialize(Spot::Archive& archive) {
    archive.Field("move", move);
    archive.Field("jump", jump);
    archive.Field("crouch", crouch);
    archive.Field("sprint", sprint);
    archive.Field("noclip", noclip);
}

void CharacterMovement::SetIntent(const CharacterIntent& intent) {
    if (!HasState(inactiveWhen) && !HasState(blockedBy)) {
        m_jump = m_jump || (intent.jump && !m_intent.jump);
        if (intent.noclip && !m_intent.noclip) {
            SetFlying(!m_flying);
        }
    }
    m_intent = intent;
}

void CharacterMovement::OnPlayerInput(PlayerController& player) {
    if (const auto actions = controls.GetActions(); m_input.GetDesc().actions != actions) {
        m_input.SetActions(actions);
    }
    m_input.Activate(&player);
    CharacterIntent intent = controls.Read(m_input);
    // Flying through everything is a cheat, as the console's noclip: only where the console permits cheats, as alone
    // or on the host, never on a session's clients.
    intent.noclip = intent.noclip && Spot::Console::Get().AreCheatsEnabled();
    SetIntent(intent);
}

void CharacterMovement::SerializeIntent(Spot::Archive& archive) {
    CharacterIntent intent = m_intent;
    // Whether it could run, as its player's machine saw it: states such as aiming or busy hands change there a frame
    // apart from where the server hears of them.
    bool sprintBlocked = HasState(sprintBlockedBy);
    archive.Field("sprintBlocked", sprintBlocked);
    archive.Field("move", intent.move);
    archive.Field("jump", intent.jump);
    archive.Field("crouch", intent.crouch);
    archive.Field("sprint", intent.sprint);
    archive.Field("noclip", intent.noclip);
    // Only where it is moved: where its player's machine moves it, that machine acted on them already.
    if (archive.IsLoading() && Spot::Network::HasAuthority(GetEntity())) {
        m_sprintBlocked = sprintBlocked;
        SetIntent(intent);
    }
}

void CharacterMovement::Replicate(Spot::Archive& archive) {
    bool grounded = m_wasGrounded;
    bool crouching = m_crouching;
    bool sprinting = m_sprinting;
    bool flying = m_flying;
    archive.Field("grounded", grounded);
    archive.Field("crouching", crouching);
    archive.Field("sprinting", sprinting);
    archive.Field("flying", flying);
    if (!archive.IsLoading()) {
        return;
    }
    m_wasGrounded = grounded;
    m_crouching = crouching;
    if (sprinting != m_sprinting) {
        SetSprinting(sprinting);
    }
    if (flying != m_flying) {
        m_flying = flying;
        if (auto* pawn = Pawn::Of(GetEntity())) {
            pawn->SetState("Noclip", flying);
        }
        UpdateBody();
    }
}

void CharacterMovement::OnPlayerUnpossess() {
    SetIntent({});
    m_jump = false;
    SetSprinting(false);
    m_input.Deactivate();
}

Spot::Vector3 CharacterMovement::GetMove() const {
    const Spot::Vector2 move = m_intent.move;
    const float length = Spot::Length(move);
    return length > 1.0f ? Spot::Vector3{move.x, 0.0f, -move.y} / length : Spot::Vector3{move.x, 0.0f, -move.y};
}

float CharacterMovement::GetSpeedLimit() const {
    if (m_inactive) {
        return std::numeric_limits<float>::infinity();
    }
    // Running and jumping at once, in the open.
    const float own = HasState(blockedBy) ? 0.0f : m_flying ? fastFlySpeed : runSpeed + jumpSpeed * 0.5f;
    return own + m_pushed;
}

void CharacterMovement::SetFlying(bool flying) {
    m_flying = flying;
    if (auto* pawn = Pawn::Of(GetEntity())) {
        pawn->SetState("Noclip", flying);
    }
    m_airVelocity = {};
    m_verticalSpeed = 0.0f;
    // Flying goes through everything, so the body is put aside meanwhile, and made again where flying ended.
    UpdateBody();
}

void CharacterMovement::UpdateBody() {
    if (auto* controller = GetEntity().GetComponent<Spot::CharacterController>()) {
        if (const bool on = !m_flying && !m_inactive; controller->IsEnabled() != on) {
            controller->SetEnabled(on);
        }
    }
}

void CharacterMovement::Push(Spot::Vector3 velocity) {
    // The machine moving it pushes it.
    Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
    if (object && object->IsDecidedByClient() && Spot::Network::IsServer(GetScene())) {
        Spot::Network::Get(GetScene())->SendToOwner(*object, CharacterPushed{velocity});
        m_pushed += Spot::Length(velocity);
        return;
    }
    m_push += Spot::Vector3{velocity.x, 0.0f, velocity.z};
    m_lift = std::max(m_lift, velocity.y);
}

void CharacterMovement::SetVelocity(Spot::Vector3 velocity) {
    m_airVelocity = {};
    m_verticalSpeed = velocity.y;
    m_push = {velocity.x, 0.0f, velocity.z};
    m_lift = 0.0f;
    m_jump = false;
    m_wasGrounded = true;
    m_landingSpeed = 0.0f;
    SetSprinting(false);
}

bool CharacterMovement::HasState(const std::vector<std::string>& states) const {
    const auto* pawn = Pawn::Of(GetEntity());
    return pawn && pawn->HasAnyState(states);
}

void CharacterMovement::SetSprinting(bool sprinting) {
    m_sprinting = sprinting;
    if (auto* pawn = Pawn::Of(GetEntity())) {
        pawn->SetState("Sprinting", sprinting);
    }
}

void CharacterMovement::OnDamage(const DamageInfo& damage) {
    const float impulse = Spot::Length(damage.impulse);
    if (impulse <= 0.0f || std::ranges::find(knockbackFrom, damage.type) == knockbackFrom.end()) {
        return;
    }
    // As much as a body of a person's weight would get, lifted a little more.
    Push(Spot::Normalize(damage.impulse / impulse + Up * 0.3f) * std::min(impulse / 70.0f, 14.0f));
}

void CharacterMovement::OnStart() {
    GetScene().GetOrAddSystem<CharacterNetwork>();
    if (const auto* controller = GetEntity().GetComponent<Spot::CharacterController>()) {
        m_standHeight = controller->GetHeight();
    }
    auto* pawn = Pawn::Of(GetEntity());
    if (const Spot::Entity* head = pawn ? GetScene().GetEntity(pawn->eye) : nullptr) {
        m_headHeight = head->GetTransform().GetPosition().y;
        m_headAcross = {head->GetTransform().GetPosition().x, head->GetTransform().GetPosition().z};
    }
    if (pawn) {
        m_states = pawn->OnStateChanged.Connect([this](std::string_view, bool) { UpdateInactive(); });
    }
    UpdateInactive();
}

void CharacterMovement::UpdateInactive() {
    // Seated in a vehicle, say: the body is put aside and nothing moves it.
    const bool inactive = HasState(inactiveWhen);
    if (inactive != m_inactive) {
        m_inactive = inactive;
        if (inactive) {
            SetFlying(false);
            SetSprinting(false);
            m_verticalSpeed = 0.0f;
            m_airVelocity = {};
            m_push = {};
            m_lift = 0.0f;
            m_jump = false;
        }
        UpdateBody();
    }
}

void CharacterMovement::OnFixedUpdate(float fixedDeltaTime) {
    if (!Spot::Network::HasAuthority(GetEntity())) {
        m_jump = false;
        return;
    }
    Step(fixedDeltaTime);
}

void CharacterMovement::Step(float fixedDeltaTime) {
    if (!m_inactive) {
        if (m_flying) {
            Fly(fixedDeltaTime);
        } else if (auto* controller = GetEntity().GetComponent<Spot::CharacterController>()) {
            Walk(*controller, fixedDeltaTime);
        }
    }
    m_jump = false;
    m_sprintBlocked.reset();
}

void CharacterMovement::Landed(float speed) {
    // As fast as it was seen falling, give or take how far in the past it is shown: falling at three times gravity,
    // it gains about 3 m/s in each tenth of a second.
    const float landing = std::min(speed, m_shownFall + 5.0f);
    if (landing > 3.0f) {
        m_landingSpeed = landing;
    }
    if (auto* character = dynamic_cast<Character*>(Pawn::Of(GetEntity()))) {
        character->OnLanded.Emit(landing);
    }
}

void CharacterMovement::OnLateUpdate(float deltaTime) {
    if (m_inactive) {
        return;
    }
    Stride(deltaTime);
    const auto* pawn = Pawn::Of(GetEntity());
    Spot::Entity* head = pawn ? GetScene().GetEntity(pawn->eye) : nullptr;
    if (!head) {
        return;
    }
    // The dead sink down to the ground.
    const float target = HasState(blockedBy) ? m_headHeight + m_standHeight * 0.5f - 0.35f
                         : m_crouching       ? m_standHeight - crouchHeight
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
    head->GetTransform().SetPosition(
        Spot::Vector3{m_headAcross.x, m_headHeight - m_headDrop + m_bob + m_dip, m_headAcross.y});
}

void CharacterMovement::Walk(Spot::CharacterController& controller, float deltaTime) {
    if (m_intent.crouch != m_crouching && (m_intent.crouch || HasRoomToStand(controller))) {
        SetCrouching(controller, m_intent.crouch);
    }

    const auto* pawn = Pawn::Of(GetEntity());
    const bool moving = !HasState(blockedBy);
    const float yaw = pawn ? PhysicsDrive::GetYaw(pawn->GetViewPoint().direction) : 0.0f;
    const Spot::Vector3 move = moving ? GetMove() : Spot::Vector3{};
    const Spot::Vector3 direction = Spot::Quaternion::FromAxisAngle(Up, yaw) * move;
    // Forward, or nearly, and once in the air only as the jump began.
    const bool blocked = m_sprintBlocked.value_or(HasState(sprintBlockedBy));
    m_sprintBlocked.reset();
    const bool sprint = m_intent.sprint && !m_crouching && -move.z > 0.5f && !blocked;
    SetSprinting(sprint && (controller.IsGrounded() || m_sprinting));
    const float speed = m_crouching ? crouchSpeed : m_sprinting ? runSpeed : walkSpeed;
    if (controller.IsGrounded()) {
        m_airVelocity = direction * speed;
    }

    if (m_lift > 0.0f) {
        m_verticalSpeed = std::max(m_verticalSpeed, m_lift);
        m_lift = 0.0f;
    } else if (controller.IsGrounded()) {
        // On the ground, the controller keeps the character on it down slopes and steps.
        m_verticalSpeed = m_jump && !m_crouching && moving ? jumpSpeed : 0.0f;
    } else {
        m_verticalSpeed += GetScene().GetOrAddSystem<Spot::PhysicsWorld>().GetGravity().y * 3.0f * deltaTime;
    }
    const float falling = -m_verticalSpeed;
    controller.Move(m_airVelocity + m_push + Spot::Vector3{0.0f, m_verticalSpeed, 0.0f}, deltaTime);
    // A push slides to a stop on the ground, and barely slows in the air.
    m_push = m_push * std::exp(-(controller.IsGrounded() ? 6.0f : 0.3f) * deltaTime);
    if (controller.IsGrounded() && !m_wasGrounded) {
        if (falling > 3.0f) {
            m_landingSpeed = falling;
        }
        if (auto* character = dynamic_cast<Character*>(Pawn::Of(GetEntity()))) {
            character->OnLanded.Emit(falling);
        }
        // The fall's damage is the server's.
        if (Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
            object && Spot::Network::IsClient(GetScene())) {
            Spot::Network::Get(GetScene())->SendToServer(*object, CharacterLanded{falling});
        }
    }
    m_wasGrounded = controller.IsGrounded();
    // A ceiling stops a jump.
    m_verticalSpeed = std::min(m_verticalSpeed, controller.GetVelocity().y);
}

void CharacterMovement::Fly(float deltaTime) {
    SetSprinting(false);
    if (HasState(blockedBy)) {
        return;
    }
    const auto* pawn = Pawn::Of(GetEntity());
    const Spot::Quaternion view = pawn ? pawn->GetViewPoint().rotation : Spot::Quaternion{};
    const float rise = static_cast<float>(m_intent.jump) - static_cast<float>(m_intent.crouch);
    Spot::Vector3 move = view * GetMove() + Up * rise;
    if (Spot::LengthSquared(move) > 1.0f) {
        move = Spot::Normalize(move);
    }
    const float speed = m_intent.sprint ? fastFlySpeed : flySpeed;
    Spot::Transform& transform = GetEntity().GetTransform();
    transform.SetPosition(transform.GetPosition() + move * (speed * deltaTime));
}

void CharacterMovement::SetCrouching(Spot::CharacterController& controller, bool crouching) {
    m_crouching = crouching;
    // The capsule shrinks down to the feet, which stay where they are.
    controller.SetHeight(crouching ? crouchHeight : m_standHeight);
    controller.SetCenter({0.0f, crouching ? (crouchHeight - m_standHeight) * 0.5f : 0.0f, 0.0f});
}

bool CharacterMovement::HasRoomToStand(const Spot::CharacterController& controller) const {
    const float radius = controller.GetRadius();
    const Spot::Vector3 top = GetEntity().GetTransform().GetWorldPosition() + controller.GetCenter() +
                              Up * (controller.GetHeight() * 0.5f - radius);
    const auto hit = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().SphereCast(
        top, radius * 0.9f, Up, m_standHeight - crouchHeight, {.ignore = GetEntity().GetId()});
    return !hit.has_value();
}

void CharacterMovement::Stride(float deltaTime) {
    const auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
    // Moved by another machine, it walks as fast as it is seen to move, on the ground unless it rises or falls fast.
    const Spot::Vector3 position = GetEntity().GetTransform().GetWorldPosition();
    m_shownFall *= std::exp(-2.0f * deltaTime);
    m_pushed *= std::exp(-0.5f * deltaTime);
    if (m_lastShown && deltaTime > 0.0f) {
        const Spot::Vector3 velocity = (position - *m_lastShown) / deltaTime;
        m_shownVelocity = Spot::Lerp(m_shownVelocity, velocity, std::min(deltaTime * 10.0f, 1.0f));
        m_shownFall = std::max(m_shownFall, -velocity.y);
    }
    m_lastShown = position;
    const bool shown = !Spot::Network::HasAuthority(GetEntity());
    const bool grounded =
        !m_flying && controller &&
        (shown ? std::abs(m_shownVelocity.y) < 2.0f : controller->IsEnabled() && controller->IsGrounded());
    float speed = 0.0f;
    if (grounded) {
        const Spot::Vector3 velocity = shown ? m_shownVelocity : controller->GetVelocity();
        speed = Spot::Length(Spot::Vector3{velocity.x, 0.0f, velocity.z});
    }
    if (m_landingSpeed > 0.0f) {
        m_dipVelocity -= std::min(m_landingSpeed * 0.06f, 0.8f);
        OnFootstep.Emit(std::clamp(m_landingSpeed / 8.0f, 0.6f, 1.6f));
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
    // Kept small, so it stays precise however long it walks.
    if (m_stepPhase > 1024.0f) {
        m_stepPhase -= 1024.0f;
        return;
    }
    if (std::floor(m_stepPhase) > std::floor(before)) {
        OnFootstep.Emit(m_crouching ? 0.45f : speed > walkSpeed * 1.2f ? 1.4f : 1.0f);
    }
}

void CharacterMovement::OnDisable() {
    OnPlayerUnpossess();
}

void CharacterMovement::Serialize(Spot::Archive& archive) {
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
    archive.Field("bobHeight", bobHeight, {.min = 0.0f, .max = 0.2f, .speed = 0.001f});
    archive.Field("knockbackFrom", knockbackFrom, {.tooltip = "The types of damage that throw it back"});
    archive.Field("sprintBlockedBy", sprintBlockedBy, {.tooltip = "Pawn states during which it cannot run"});
    archive.Field("blockedBy", blockedBy, {.tooltip = "Pawn states during which it neither moves nor jumps"});
    archive.Field("inactiveWhen", inactiveWhen, {.tooltip = "Pawn states during which its body is put aside"});
    archive.Field("controls", controls, {.tooltip = "The actions a player moves it with"});
}

} // namespace Gameplay
