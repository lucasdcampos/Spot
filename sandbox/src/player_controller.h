#pragma once

#include <spot.h>

// Turns the player's head with the mouse while the cursor is locked, and switches its flashlight with F. The body never
// turns; only the head does. PlayerMovement walks where it looks, and WeaponHolder fires where it aims.
class PlayerController final : public Spot::Component {
public:
    // Radians per pixel the mouse moves.
    float lookSensitivity = 0.0025f;
    // The entity looking where the player looks, such as the one with the camera.
    Spot::EntityId head;
    Spot::EntityId flashlight;

    // Where the player looks: the yaw turns around Y, and the view adds the pitch.
    float GetYaw() const { return m_yaw; }
    Spot::Quaternion GetViewRotation() const;
    // Where the player's eyes are, at its head, and the unit vector they look along.
    Spot::Vector3 GetEyePosition() const;
    Spot::Vector3 GetAimDirection() const;

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
