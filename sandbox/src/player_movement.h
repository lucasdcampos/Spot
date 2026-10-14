#pragma once

#include <spot.h>

// Moves the player's CharacterController where its PlayerController looks, with the actions: Move to walk, Sprint to
// run, Jump to jump and Crouch to crouch, standing up again only once there is room above. Steps sound like the
// ground they land on, quieter crouching and louder running, in a cadence that slows as strides lengthen. Noclip
// switches flying through everything: Jump goes up, Crouch down and Sprint faster. Falling below the world brings the
// player back to the start.
class PlayerMovement final : public Spot::Component {
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
    float fallHeight = -20.0f;
    // Meters between footsteps at walking speed; strides lengthen with speed, as running ones do.
    float strideLength = 2.1f;
    float footstepVolume = 0.35f;
    // How far the head dips at each footstep, in meters, while walking.
    float bobHeight = 0.012f;

    bool IsCrouching() const { return m_crouching; }
    bool IsFlying() const { return m_flying; }
    void SetFlying(bool flying);
    float GetCameraBob() const { return m_bob; }
    // Footsteps taken, with the share of the current one: the walk cycle the head and the held item bob with. Each
    // footstep lands as it reaches a whole number.
    float GetStepPhase() const { return m_stepPhase; }
    // How much walking bobs the view now: 0 standing still, 1 walking and more running, eased in and out.
    float GetBobWeight() const { return m_bobWeight; }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Walk(Spot::CharacterController& controller, float deltaTime);
    void Fly(float deltaTime);
    void SetCrouching(Spot::CharacterController& controller, bool crouching);
    // Whether the standing capsule would fit where the crouching one is.
    bool HasRoomToStand(const Spot::CharacterController& controller) const;
    // Advances the walk cycle by the distance walked on the ground, playing a step as each stride ends.
    void Stride(float deltaTime);
    void PlayFootstep(float loudness);

    Spot::Vector3 m_spawnPoint;
    float m_standHeight = 1.8f;
    float m_headHeight = 0.7f;
    // How far the head is lowered, easing toward where crouching puts it.
    float m_headDrop = 0.0f;
    float m_bob = 0.0f;
    // Up while jumping, down while falling; gravity changes it while the player is off the ground.
    float m_verticalSpeed = 0.0f;
    bool m_jump = false;
    bool m_crouchHeld = false;
    bool m_crouching = false;
    bool m_flying = false;
    bool m_wasGrounded = true;
    float m_landingSpeed = 0.0f;
    float m_stepPhase = 0.5f;
    float m_bobWeight = 0.0f;
    // The dip landing gives the head, as a damped spring.
    float m_dip = 0.0f;
    float m_dipVelocity = 0.0f;
    std::size_t m_footstep = 0;
    std::uint32_t m_random = 0x9e3779b9u;
};
