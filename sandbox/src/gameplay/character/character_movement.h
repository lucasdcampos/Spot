#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/core/player_input.h"
#include "gameplay/health/damage.h"

#include <spot.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Gameplay {

// What a character is asked to do, by whoever controls it: held controls, which it acts on as they change.
struct CharacterIntent {
    // Where to go as it faces: x to the right and y forward, a stick pushed part of the way going slower.
    Spot::Vector2 move;
    bool jump = false;
    bool crouch = false;
    bool sprint = false;
    // Switches flying through everything as it is pressed.
    bool noclip = false;
};

// The actions a player's controls give a character its intent from.
struct CharacterControls {
    std::string move = "Move";
    std::string jump = "Jump";
    std::string crouch = "Crouch";
    std::string sprint = "Sprint";
    std::string noclip = "Noclip";

    std::vector<std::string> GetActions() const;
    CharacterIntent Read(const InputContext& input) const;
    void Serialize(Spot::Archive& archive);
};

// Moves the character's CharacterController where its pawn looks, as its intent asks: to walk, to run forward, to
// jump and to crouch, standing up again only once there is room above. A player's controller gives the intent from
// its controls; any other, or a test, sets it (SetIntent()). Running is blocked while the pawn is in a state of
// sprintBlockedBy, as aiming or with its hands busy, and while running the pawn is "Sprinting". It walks a cycle of
// steps (GetStepPhase()), slower as strides lengthen, telling each footstep (OnFootstep) and landing (the Character's
// OnLanded). Damage of the knockback types throws it back (Push()). Noclip switches flying through everything: jump
// goes up, crouch down and sprint faster. In a state of blockedBy, as dead, it neither moves nor jumps; in one of
// inactiveWhen, as seated,
// its body is put aside and it does nothing. Only where its pawn's state is decided does it move it (see
// Spot::Network::HasAuthority()); elsewhere the pawn moves as it is told.
class CharacterMovement : public Spot::Component, public DamageReceiver, public PlayerInputListener {
public:
    // Meters per second.
    float walkSpeed = 5.0f;
    float runSpeed = 9.0f;
    float crouchSpeed = 2.5f;
    float jumpSpeed = 5.5f;
    float flySpeed = 10.0f;
    float fastFlySpeed = 25.0f;
    // The capsule's height while crouching, from end to end.
    float crouchHeight = 1.0f;
    // Meters between footsteps at walking speed; strides lengthen with speed, as running ones do.
    float strideLength = 2.1f;
    // How far the head dips at each footstep, in meters, while walking.
    float bobHeight = 0.012f;
    // The types of damage that throw the character back, as hard as they would a body of a person's weight.
    std::vector<std::string> knockbackFrom = {"Explosion"};
    std::vector<std::string> sprintBlockedBy = {"Aiming", "HandsBusy", "Dead"};
    std::vector<std::string> blockedBy = {"Dead"};
    std::vector<std::string> inactiveWhen = {"Seated"};
    CharacterControls controls;

    // What it does when no player controls it; a player's controls replace it each frame.
    void SetIntent(const CharacterIntent& intent);
    const CharacterIntent& GetIntent() const { return m_intent; }

    bool IsCrouching() const { return m_crouching; }
    // Running forward, on the ground or in a jump that began running.
    bool IsSprinting() const { return m_sprinting; }
    // Adds to the velocity, in meters per second: what it gains sideways slows down as it slides on the ground, and
    // what it gains upward lifts it off.
    void Push(Spot::Vector3 velocity);
    // Replaces its velocity, clearing pending jumps and pushes, as when its pawn is placed elsewhere.
    void SetVelocity(Spot::Vector3 velocity);
    bool IsFlying() const { return m_flying; }
    void SetFlying(bool flying);
    float GetCameraBob() const { return m_bob; }
    // Footsteps taken, with the share of the current one: the walk cycle the head and the held item bob with. Each
    // footstep lands as it reaches a whole number.
    float GetStepPhase() const { return m_stepPhase; }
    // How much walking bobs the view now: 0 standing still, 1 walking and more running, eased in and out.
    float GetBobWeight() const { return m_bobWeight; }

    // Each step it takes and each landing, with how loud it is: 1 walking, less crouching and more running.
    Spot::Signal<float> OnFootstep;

protected:
    bool IsInputBeforeFixedSteps() const override { return true; }
    void OnPlayerInput(PlayerController& player) override;
    void OnPlayerUnpossess() override;
    void SerializeIntent(Spot::Archive& archive) override;
    void OnDamage(const DamageInfo& damage) override;
    void OnStart() override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Where it goes, in its space as it faces: -Z is forward.
    Spot::Vector3 GetMove() const;
    void Walk(Spot::CharacterController& controller, float deltaTime);
    void Fly(float deltaTime);
    void SetCrouching(Spot::CharacterController& controller, bool crouching);
    // Whether the standing capsule would fit where the crouching one is.
    bool HasRoomToStand(const Spot::CharacterController& controller) const;
    // Advances the walk cycle by the distance walked on the ground, telling a step as each stride ends.
    void Stride(float deltaTime);
    void SetSprinting(bool sprinting);
    bool HasState(const std::vector<std::string>& states) const;
    // Turns the body on while it moves on the ground, and off while flying or put aside.
    void UpdateBody();
    void UpdateInactive();

    float m_standHeight = 1.8f;
    float m_headHeight = 0.7f;
    // How far the head is lowered, easing toward where crouching puts it.
    float m_headDrop = 0.0f;
    float m_bob = 0.0f;
    // Up while jumping, down while falling; gravity changes it while it is off the ground.
    float m_verticalSpeed = 0.0f;
    InputContext m_input{{.name = "Movement", .actions = {"Move", "Jump", "Crouch", "Sprint", "Noclip"}}};
    CharacterIntent m_intent;
    bool m_jump = false;
    bool m_crouching = false;
    bool m_flying = false;
    bool m_sprinting = false;
    bool m_inactive = false;
    Spot::Vector3 m_push;
    float m_lift = 0.0f;
    bool m_wasGrounded = true;
    float m_landingSpeed = 0.0f;
    float m_stepPhase = 0.5f;
    float m_bobWeight = 0.0f;
    // The dip landing gives the head, as a damped spring.
    float m_dip = 0.0f;
    float m_dipVelocity = 0.0f;
    Spot::SignalConnection m_states;
};

} // namespace Gameplay
