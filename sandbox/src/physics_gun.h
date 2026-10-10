#pragma once

#include <spot.h>

// The tool on Weapon 1 (see WeaponHolder). Holding Fire grabs the prop the player aims at, anywhere in range, and
// drives the point it grabbed toward where it was grabbed in front of the camera, as on a spring; Switch Weapon (the
// wheel, or the shoulders) pulls it closer or pushes it away, and holding Use turns it with Look. Aim freezes it where
// it is, as a kinematic body, and Unfreeze unfreezes every frozen prop. Frozen props carry the tag "Frozen".
class PhysicsGun final : public Spot::Component {
public:
    static constexpr std::string_view FrozenTag = "Frozen";

    float range = 60.0f;
    float minDistance = 1.5f;
    // Meters the wheel moves the prop per notch; a shoulder held moves it a tenth of that each frame.
    float scrollStep = 1.0f;
    float maxSpeed = 40.0f;
    // An entity whose AudioSource hums while the gun holds something, higher the faster the prop moves.
    Spot::EntityId hum;

    bool IsGrabbing() const { return m_grabbed != Spot::EntityId::Invalid; }
    Spot::EntityId GetGrabbed() const { return m_grabbed; }
    bool IsTurning() const { return IsGrabbing() && m_turning; }

    // Freezes the grabbed prop and lets it go.
    void Freeze();
    void Release();
    void UnfreezeAll();

protected:
    void OnUpdate(float deltaTime) override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    bool IsActive() const;
    Spot::AudioSource* GetHum() const;
    void Grab();
    Spot::Rigidbody* GetGrabbedBody() const;

    Spot::EntityId m_grabbed = Spot::EntityId::Invalid;
    // Where it was grabbed, relative to the prop.
    Spot::Vector3 m_grabPoint;
    float m_distance = 0.0f;
    // The prop's rotation relative to where the player faces, so it turns with the player.
    Spot::Quaternion m_rotation;
    bool m_turning = false;
};
