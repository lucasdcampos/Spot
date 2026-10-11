#pragma once

#include <spot.h>

// Moves the player's CharacterController where its PlayerController looks, with the actions: Move to walk, Sprint to
// run, Jump to jump and Crouch to crouch, standing up again only once there is room above. Steps sound like the
// ground they land on, quieter crouching and louder running. Noclip switches flying
// through everything: Jump goes up, Crouch down and Sprint faster. Falling below the world brings the player back to
// the start.
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
    // Meters walked between footsteps, which sound like the ground under the player; see Surface.
    float strideLength = 0.8f;
    float footstepVolume = 0.5f;
    float bobHeight = 0.025f;
    float bobPeriod = 0.35f;

    bool IsCrouching() const { return m_crouching; }
    bool IsFlying() const { return m_flying; }
    void SetFlying(bool flying);
    float GetCameraBob() const { return m_bob; }

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
    // Counts the distance walked on the ground, and plays a step each stride.
    void Step(const Spot::CharacterController& controller, float distance);
    bool IsWalking() const;
    Spot::Task BobCamera();

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
    float m_stride = 0.0f;
    std::size_t m_footstep = 0;
};
