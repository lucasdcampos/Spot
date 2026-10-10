#pragma once

#include <spot.h>

// Turns the player's head with the mouse and throws balls with a click while the cursor is locked, and switches its
// flashlight with F. The body never turns; only the head does, and PlayerMovement walks where it looks.
class PlayerController final : public Spot::Component {
public:
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
