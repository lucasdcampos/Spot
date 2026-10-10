#pragma once

#include <spot.h>

// The second tool. Holding the left button grabs the prop the player aims at, anywhere in range, and drives the point
// it grabbed toward where it was grabbed in front of the camera, as on a spring; the wheel pulls it closer or pushes
// it away, and holding E turns it with the mouse. The right button freezes it where it is, as a kinematic body, and U
// unfreezes every frozen prop. Frozen props carry the tag "Frozen".
class PhysicsGun final : public Spot::Component {
public:
    static constexpr std::string_view FrozenTag = "Frozen";

    float range = 60.0f;
    float minDistance = 1.5f;
    // Meters the wheel moves the prop per notch.
    float scrollStep = 1.0f;
    float maxSpeed = 40.0f;

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
