#pragma once

#include <spot.h>

// Turns the player's head with the Look action while the cursor is locked, and switches its flashlight with the
// Flashlight action. The body never turns; only the head does. PlayerMovement walks where it looks, and WeaponHolder
// fires where it aims. Aiming down the sights narrows the head's camera, and slows the look as much.
class PlayerController final : public Spot::Component {
public:
    // Radians per pixel the mouse moves.
    float lookSensitivity = 0.0025f;
    // How fast the view zooms in and out, as a share of the way each 1/60 of a second.
    float zoomSpeed = 0.25f;
    // The entity looking where the player looks, such as the one with the camera.
    Spot::EntityId head = Spot::EntityId::Invalid;
    Spot::EntityId flashlight = Spot::EntityId::Invalid;

    // Where the player looks: the yaw turns around Y, and the view adds the pitch.
    float GetYaw() const { return m_yaw; }
    Spot::Quaternion GetViewRotation() const;
    // Where the player's eyes are, at its head, and the unit vector they look along.
    Spot::Vector3 GetEyePosition() const;
    Spot::Vector3 GetAimDirection() const;

    void ToggleFlashlight();
    // How many times the view is magnified now, 1 when not aiming.
    float GetZoom() const { return m_zoom; }
    float GetFieldOfView() const;
    void SetFieldOfView(float radians);
    void Kick(float radians, float recoveryTime = 0.2f);
    float GetRecoil() const { return m_recoil; }

protected:
    void OnStart() override;
    void OnDisable() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::Entity* GetHead() const;

    float m_yaw = 0.0f;
    float m_pitch = -0.15f;
    float m_zoom = 1.0f;
    // The head camera's field of view without zoom.
    float m_fieldOfView = 0.0f;
    float m_recoil = 0;
    // From 0 to 1 as the player runs, widening the view a little.
    float m_run = 0.0f;
    Spot::TweenId m_recoilTween = Spot::TweenId::Invalid;
};
