#pragma once

#include "spot/core/vector.h"
#include "spot/physics/physics_3d.h"
#include "spot/scene/component.h"
#include "spot/scene/entity.h"

namespace Spot {

class Transform;

// Joins its entity's body to another entity's body, or to the world, as the bodies are when the joint is made: when
// it starts, and again whenever it or either body changes. Its entity needs a rigidbody; the other entity, any
// collider or rigidbody. The kinds of joint derive from it:
//
//     auto& hinge = door.AddComponent<Spot::HingeJoint>();
//     hinge.SetConnectedEntity(frame.GetId());
//     hinge.SetAnchor({-0.5f, 0.0f, 0.0f});
//
// A joint with a break force breaks when holding its bodies together takes more than that force: it is disabled,
// IsBroken() turns true, and the components of its entity get Component::OnJointBreak().
class Joint : public Component {
public:
    // The entity whose body this one is joined to; EntityId::Invalid joins it to the world.
    EntityId GetConnectedEntity() const { return m_connected; }
    void SetConnectedEntity(EntityId entity);
    // Where the joint is, relative to the entity.
    Vector3 GetAnchor() const { return m_anchor; }
    void SetAnchor(Vector3 anchor);
    // In newtons; 0 never breaks.
    float GetBreakForce() const { return m_breakForce; }
    void SetBreakForce(float force);
    bool IsBroken() const { return m_broken; }
    // How hard the joint held its bodies together in the last physics step, in newtons.
    float GetForce() const;

protected:
    Joint() = default;

    void OnEnable() override;
    void OnDisable() override;
    void Serialize(Archive& archive) override;
    void OnValidate() override { MarkChanged(); }

    // Makes the joint again with its new values.
    void MarkChanged();
    // The joint as it is in the physics, while it is; for the kinds that read or change it.
    JointId3D GetJointId() const;

private:
    friend class PhysicsWorld;

    // Fills in what the kind of joint decides, from where its entity, and the connected one if any, are.
    virtual void Describe(JointDesc3D& desc, const Transform& transform, const Transform* connected) const = 0;

    EntityId m_connected = EntityId::Invalid;
    Vector3 m_anchor = {};
    float m_breakForce = 0.0f;
    bool m_broken = false;
};

// Holds the bodies together as they are, as a weld does.
class FixedJoint final : public Joint {
private:
    void Describe(JointDesc3D& desc, const Transform& transform, const Transform* connected) const override;
};

// Lets the bodies turn around an axis through the anchor, as a door or a wheel does, within limits and driven by a
// motor when asked.
class HingeJoint final : public Joint {
public:
    // Relative to the entity.
    Vector3 GetAxis() const { return m_axis; }
    void SetAxis(Vector3 axis);
    // Angles in radians around the axis, counterclockwise as the axis points at the viewer, of the entity turning
    // relative to what it is joined to, from where the joint was made: the minimum from -pi to 0, the maximum from 0
    // to pi.
    bool HasLimits() const { return m_limits; }
    void SetLimits(bool limits, float minAngle = 0.0f, float maxAngle = 0.0f);
    float GetMinAngle() const { return m_minAngle; }
    float GetMaxAngle() const { return m_maxAngle; }
    // Turns the hinge at a speed, in radians per second, with up to a torque, in newton meters.
    bool HasMotor() const { return m_motor; }
    void SetMotor(bool motor, float speed = 0.0f, float maxTorque = 0.0f);
    float GetMotorSpeed() const { return m_motorSpeed; }
    float GetMotorTorque() const { return m_motorTorque; }
    // How far it turned from where it was made, in radians.
    float GetAngle() const;

protected:
    void Serialize(Archive& archive) override;

private:
    void Describe(JointDesc3D& desc, const Transform& transform, const Transform* connected) const override;

    Vector3 m_axis = {0.0f, 1.0f, 0.0f};
    bool m_limits = false;
    float m_minAngle = -1.5707963f;
    float m_maxAngle = 1.5707963f;
    bool m_motor = false;
    float m_motorSpeed = 0.0f;
    float m_motorTorque = 100.0f;
};

// Keeps the anchor and a point of the connected entity within a range of distances, as a rope does.
class DistanceJoint final : public Joint {
public:
    // Relative to the connected entity, or in the world without one.
    Vector3 GetConnectedAnchor() const { return m_connectedAnchor; }
    void SetConnectedAnchor(Vector3 anchor);
    // A negative maximum is the distance when the joint is made.
    float GetMinDistance() const { return m_minDistance; }
    float GetMaxDistance() const { return m_maxDistance; }
    void SetDistance(float minDistance, float maxDistance);

protected:
    void Serialize(Archive& archive) override;

private:
    void Describe(JointDesc3D& desc, const Transform& transform, const Transform* connected) const override;

    Vector3 m_connectedAnchor = {};
    float m_minDistance = 0.0f;
    float m_maxDistance = -1.0f;
};

// Pulls the anchor and a point of the connected entity toward a distance with a spring.
class SpringJoint final : public Joint {
public:
    // Relative to the connected entity, or in the world without one.
    Vector3 GetConnectedAnchor() const { return m_connectedAnchor; }
    void SetConnectedAnchor(Vector3 anchor);
    // A negative length is the distance when the joint is made.
    float GetRestLength() const { return m_restLength; }
    void SetRestLength(float length);
    // How fast it bounces, in hertz, and how quickly the bouncing dies down, from 0 to 1 and beyond.
    float GetFrequency() const { return m_frequency; }
    void SetFrequency(float hertz);
    float GetDamping() const { return m_damping; }
    void SetDamping(float damping);

protected:
    void Serialize(Archive& archive) override;

private:
    void Describe(JointDesc3D& desc, const Transform& transform, const Transform* connected) const override;

    Vector3 m_connectedAnchor = {};
    float m_restLength = -1.0f;
    float m_frequency = 2.0f;
    float m_damping = 0.5f;
};

// Lets the bodies turn around the anchor, the twist axis swinging within a cone and twisting within limits, as the
// bones of ragdolls do.
class SwingTwistJoint final : public Joint {
public:
    // Relative to the entity, usually along the bone.
    Vector3 GetTwistAxis() const { return m_twistAxis; }
    void SetTwistAxis(Vector3 axis);
    // The half angle of the cone, in radians.
    float GetSwingLimit() const { return m_swingLimit; }
    void SetSwingLimit(float radians);
    // From -pi to 0, and from 0 to pi.
    float GetTwistMin() const { return m_twistMin; }
    float GetTwistMax() const { return m_twistMax; }
    void SetTwistLimits(float minAngle, float maxAngle);

protected:
    void Serialize(Archive& archive) override;

private:
    void Describe(JointDesc3D& desc, const Transform& transform, const Transform* connected) const override;

    Vector3 m_twistAxis = {0.0f, 1.0f, 0.0f};
    float m_swingLimit = 0.785398f;
    float m_twistMin = -0.5235988f;
    float m_twistMax = 0.5235988f;
};

} // namespace Spot
