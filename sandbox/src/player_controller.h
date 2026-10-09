#pragma once

#include <spot.h>

// Walks a capsule with WASD, Shift to run and Space to jump, turns its head with the mouse and throws balls with a
// click while the cursor is locked, and switches its flashlight with F. The body never turns, so it cannot tip over;
// only the head does.
class PlayerController final : public Spot::Component {
public:
    // Meters per second.
    float walkSpeed = 5.0f;
    float runSpeed = 9.0f;
    float jumpSpeed = 5.5f;
    // Radians per pixel the mouse moves.
    float lookSensitivity = 0.0025f;
    // The entity looking where the player looks, such as the one with the camera.
    Spot::EntityId head;
    Spot::EntityId flashlight;
    Spot::AssetRef<Spot::Material> ballMaterial;
    float ballSpeed = 25.0f;
    // The player goes back to where it started after falling below this height.
    float fallHeight = -20.0f;

    Spot::Quaternion GetViewRotation() const;

    // Throws a ball from the head, where the player looks.
    void Shoot();
    void ToggleFlashlight();

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::Entity* GetHead() const;

    Spot::Vector3 m_spawnPoint;
    float m_yaw = 0.0f;
    float m_pitch = -0.15f;
    bool m_jump = false;
};
