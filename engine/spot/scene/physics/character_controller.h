#pragma once

#include "spot/math/vector.h"
#include "spot/scene/component.h"
#include "spot/scene/entity.h"

namespace Spot {

// Moves its entity as a character walks, under the game's control rather than by forces: a capsule standing along Y,
// centered on the entity, which slides along what it hits, climbs steps, keeps to the ground going down slopes and
// steps, and pushes dynamic bodies out of its way. The game gives it a velocity each frame, gravity included:
//
//     void Player::OnFixedUpdate(float deltaTime) {
//         auto& controller = *GetEntity().GetComponent<Spot::CharacterController>();
//         m_fall = controller.IsGrounded() ? 0.0f : m_fall - 9.81f * deltaTime;
//         controller.Move(walk + Spot::Vector3{0.0f, m_fall, 0.0f}, deltaTime);
//     }
//
// Bodies, triggers and queries see the character as a capsule, it collides with the layers its entity's layer does,
// and it gets collision and trigger events like a body. The entity needs no collider or rigidbody of its own, and its
// scale does not change the capsule.
class CharacterController final : public Component {
public:
    CharacterController() = default;

    float GetRadius() const { return m_radius; }
    void SetRadius(float radius);
    // From end to end, the caps included.
    float GetHeight() const { return m_height; }
    void SetHeight(float height);
    // Where the capsule is centered, relative to the entity.
    Vector3 GetCenter() const { return m_center; }
    void SetCenter(Vector3 center);
    // The steepest slope it stands on, in radians; it slides down steeper ones.
    float GetSlopeLimit() const { return m_slopeLimit; }
    void SetSlopeLimit(float radians);
    // The highest step it climbs, and steps down without leaving the ground.
    float GetStepHeight() const { return m_stepHeight; }
    void SetStepHeight(float height);
    // In kilograms, for how hard it pushes and presses on dynamic bodies.
    float GetMass() const { return m_mass; }
    void SetMass(float mass);
    // The largest force it pushes dynamic bodies with, in newtons.
    float GetPushStrength() const { return m_pushStrength; }
    void SetPushStrength(float strength);

    // Moves the character with the velocity for the time, in units per second, and the entity with it. It goes as
    // far as what is in the way lets it, sliding along walls. Call it once per update or fixed update.
    void Move(Vector3 velocity, float deltaTime);

    // Standing on ground no steeper than the slope limit, as of the last move.
    bool IsGrounded() const;
    // The feet have support, including ground steeper than the slope limit. A wall touching the side does not count.
    // Games can stop applying gravity on steep support without allowing the character to walk up it.
    bool IsSupported() const;
    // The normal of the ground under the character, and the velocity of that ground, such as a moving platform's.
    Vector3 GetGroundNormal() const;
    Vector3 GetGroundVelocity() const;
    // The entity of the ground under the character; EntityId::Invalid in the air.
    EntityId GetGround() const;
    // The velocity it moved with in the last move, after what it hit changed it.
    Vector3 GetVelocity() const;

protected:
    void OnEnable() override;
    void OnDisable() override;
    void Serialize(Archive& archive) override;
    void OnValidate() override { MarkChanged(); }

private:
    friend class PhysicsWorld;

    void MarkChanged();

    float m_radius = 0.4f;
    float m_height = 1.8f;
    Vector3 m_center = {};
    float m_slopeLimit = 0.785398f;
    float m_stepHeight = 0.35f;
    float m_mass = 70.0f;
    float m_pushStrength = 200.0f;
};

} // namespace Spot
