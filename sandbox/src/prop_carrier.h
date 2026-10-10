#pragma once

#include <spot.h>

// Use picks up the prop the player looks at, when it weighs no more than maxMass, and carries it in front of the
// camera; Use again drops it. Fire throws it, and holding Aim turns it with Look.
class PropCarrier final : public Spot::Component {
public:
    float reach = 2.5f;
    // In kilograms.
    float maxMass = 35.0f;
    // How far in front of the camera the prop is carried, in meters.
    float holdDistance = 1.6f;
    float throwSpeed = 12.0f;

    bool IsHolding() const { return m_held != Spot::EntityId::Invalid; }
    Spot::EntityId GetHeld() const { return m_held; }
    // While turning the prop, the mouse turns it rather than the view.
    bool IsTurning() const { return IsHolding() && m_turning; }
    // Whether the carrier has Fire this frame, to throw what it carries, so the tool in hand leaves it alone.
    bool HasClick() const { return IsHolding() || m_threw; }

    void PickUp(Spot::Entity& prop);
    void Drop();
    void Throw();

protected:
    void OnUpdate(float deltaTime) override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // The held prop's rigidbody, while it still has one.
    Spot::Rigidbody* GetHeldBody() const;

    Spot::EntityId m_held = Spot::EntityId::Invalid;
    // Its rotation relative to where the player faces, so it turns with the player.
    Spot::Quaternion m_rotation;
    bool m_turning = false;
    bool m_threw = false;
};
