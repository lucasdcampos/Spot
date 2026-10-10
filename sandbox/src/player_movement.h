#pragma once

#include <spot.h>

// Moves the player's CharacterController where its PlayerController looks: WASD to walk, Shift to run, Space to jump
// and Ctrl to crouch, standing up again only once there is room above. V switches noclip, which flies through
// everything: Space goes up, Ctrl down and Shift faster. Falling below the world brings the player back to the start.
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

    bool IsCrouching() const { return m_crouching; }
    bool IsFlying() const { return m_flying; }
    void SetFlying(bool flying);

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

    Spot::Vector3 m_spawnPoint;
    float m_standHeight = 1.8f;
    float m_headHeight = 0.7f;
    // How far the head is lowered, easing toward where crouching puts it.
    float m_headDrop = 0.0f;
    // Up while jumping, down while falling; gravity changes it while the player is off the ground.
    float m_verticalSpeed = 0.0f;
    bool m_jump = false;
    bool m_crouchHeld = false;
    bool m_crouching = false;
    bool m_flying = false;
};
