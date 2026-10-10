#pragma once

#include <spot.h>

// Turns the player's head with the mouse while the cursor is locked, picks the tool in hand with 1 and 2, throws balls
// with a click while the launcher is in hand, and switches its flashlight with F. The body never turns; only the head
// does, and PlayerMovement walks where it looks.
class PlayerController final : public Spot::Component {
public:
    // What a click does: the launcher throws balls, and the physics gun (PhysicsGun) grabs props.
    enum class Tool { Launcher, PhysicsGun };

    Tool activeTool = Tool::Launcher;
    // Radians per pixel the mouse moves.
    float lookSensitivity = 0.0025f;
    // The entity looking where the player looks, such as the one with the camera.
    Spot::EntityId head;
    Spot::EntityId flashlight;
    Spot::AssetRef<Spot::Material> ballMaterial;
    float ballSpeed = 25.0f;

    // Where the player looks: the yaw turns around Y, and the view adds the pitch.
    float GetYaw() const { return m_yaw; }
    Spot::Quaternion GetViewRotation() const;
    // Where the player's eyes are, at its head, and the unit vector they look along.
    Spot::Vector3 GetEyePosition() const;
    Spot::Vector3 GetAimDirection() const;

    // Throws a ball from the head, where the player looks.
    void Shoot();
    void ToggleFlashlight();

protected:
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::Entity* GetHead() const;

    float m_yaw = 0.0f;
    float m_pitch = -0.15f;
};
