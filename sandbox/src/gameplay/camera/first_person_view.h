#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/core/user_settings.h"

#include <spot.h>

#include <string>

namespace Gameplay {

// Turns its pawn's head with the look action while the cursor is locked, read through the pawn's controller; the body
// never turns, only the head does. What the pawn's parts magnify its view by (Pawn::GetZoom()), as a firearm aimed
// down its sights, narrows the head's camera and slows the look as much, and it widens a little while the pawn is
// "Sprinting". A CameraShake on the pawn shakes the view, not the aim; Kick() kicks both, as recoil. It takes the look
// sensitivity and field of view the player sets.
class FirstPersonView : public Spot::Component, public UserSettingsListener {
public:
    // Radians per pixel the mouse moves.
    float lookSensitivity = 0.0025f;
    // How fast the view zooms in and out, as a share of the way each 1/60 of a second.
    float zoomSpeed = 0.25f;
    // The entity looking where the pawn looks, such as the one with the camera.
    Spot::EntityId head = Spot::EntityId::Invalid;
    std::string lookAction = "Look";
    // The pawn state during which the view widens.
    std::string runState = "Sprinting";

    // Where the pawn looks: the yaw turns around Y, and the view adds the pitch.
    float GetYaw() const { return m_yaw; }
    // Looks that way at once, as on getting out of a vehicle.
    void SetLook(float yaw, float pitch);
    Spot::Quaternion GetViewRotation() const;
    // Where the eyes are, at the head, and the unit vector they look along.
    Spot::Vector3 GetEyePosition() const;
    Spot::Vector3 GetAimDirection() const;

    // How many times the view is magnified now, 1 when not aiming.
    float GetZoom() const { return m_zoom; }
    float GetFieldOfView() const;
    void SetFieldOfView(float radians);
    // Kicks the view and the aim up by the angle, coming back down over the seconds.
    void Kick(float radians, float recoveryTime = 0.2f);
    float GetRecoil() const { return m_recoil; }

protected:
    void ApplySettings(const UserSettings& settings) override;
    void ReadSettings(UserSettings& settings) const override;
    void OnStart() override;
    void OnDisable() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::Entity* GetHead() const;
    // Turns the head where the pawn looks, which is where its view point looks.
    void TurnHead(Spot::Quaternion rotation);

    float m_yaw = 0.0f;
    float m_pitch = -0.15f;
    float m_zoom = 1.0f;
    // The head camera's field of view without zoom.
    float m_fieldOfView = 0.0f;
    float m_recoil = 0;
    // From 0 to 1 as the pawn runs, widening the view a little.
    float m_run = 0.0f;
    InputContext m_input{{.name = "Look", .actions = {"Look"}}};
    Spot::TweenId m_recoilTween = Spot::TweenId::Invalid;
};

} // namespace Gameplay
