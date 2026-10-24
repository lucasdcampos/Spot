#include "spot/scene/physics/joint.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"
#include "spot/scene/physics/physics_world.h"
#include "spot/scene/scene.h"

#include <cmath>

namespace Spot {

namespace {

Vector3 ToWorld(const Transform& transform, Vector3 point) {
    const Vector4 world = transform.GetWorldMatrix() * Vector4{point.x, point.y, point.z, 1.0f};
    return {world.x, world.y, world.z};
}

// Any unit vector perpendicular to the axis, for joints whose angle is counted from it.
Vector3 GetPerpendicular(Vector3 axis) {
    const Vector3 other = std::abs(axis.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
    return Normalize(Cross(axis, other));
}

} // namespace

void Joint::SetConnectedEntity(EntityId entity) {
    m_connected = entity;
    MarkChanged();
}

void Joint::SetAnchor(Vector3 anchor) {
    m_anchor = anchor;
    MarkChanged();
}

void Joint::SetBreakForce(float force) {
    m_breakForce = force;
}

float Joint::GetForce() const {
    return GetScene().GetPhysics().GetJointForce(*this);
}

void Joint::OnEnable() {
    m_broken = false;
    GetScene().GetPhysics().MarkJointChanged(*this);
}

void Joint::OnDisable() {
    GetScene().GetPhysics().ForgetJoint(*this);
}

void Joint::Serialize(Archive& archive) {
    archive.Field("connectedEntity", m_connected, {.tooltip = "The entity it joins to; none joins to the world"});
    archive.Field("anchor", m_anchor, {.speed = 0.01f, .tooltip = "Where the joint is, from the entity"});
    archive.Field("breakForce", m_breakForce,
                  {.min = 0.0f, .max = 1.0e9f, .speed = 10.0f, .tooltip = "In newtons; 0 never breaks"});
}

void Joint::MarkChanged() {
    if (IsActiveAndEnabled()) {
        GetScene().GetPhysics().MarkJointChanged(*this);
    }
}

Detail::PhysicsJointId Joint::GetJointId() const {
    return GetScene().GetPhysics().FindJoint(*this);
}

void FixedJoint::Describe(Detail::PhysicsJointDesc& desc, const Transform& transform,
                          const Transform* /*connected*/) const {
    desc.kind = Detail::PhysicsJointKind::Fixed;
    desc.point = ToWorld(transform, GetAnchor());
}

void HingeJoint::SetAxis(Vector3 axis) {
    m_axis = axis;
    MarkChanged();
}

void HingeJoint::SetLimits(bool limits, float minAngle, float maxAngle) {
    m_limits = limits;
    m_minAngle = minAngle;
    m_maxAngle = maxAngle;
    MarkChanged();
}

void HingeJoint::SetMotor(bool motor, float speed, float maxTorque) {
    m_motor = motor;
    m_motorSpeed = speed;
    m_motorTorque = maxTorque;
    // Changed in place, so the hinge keeps counting its angle from where it was made.
    GetScene().GetPhysics().SetHingeMotor(*this, motor, speed, maxTorque);
}

float HingeJoint::GetAngle() const {
    return GetScene().GetPhysics().GetHingeAngle(*this);
}

void HingeJoint::Serialize(Archive& archive) {
    Joint::Serialize(archive);
    archive.Field("axis", m_axis, {.speed = 0.01f, .tooltip = "What it turns around, relative to the entity"});
    archive.Field("limits", m_limits);
    archive.Field("minAngle", m_minAngle, {.min = -3.14159265f, .max = 0.0f, .angle = true});
    archive.Field("maxAngle", m_maxAngle, {.min = 0.0f, .max = 3.14159265f, .angle = true});
    archive.Field("motor", m_motor);
    archive.Field("motorSpeed", m_motorSpeed, {.angle = true, .tooltip = "Per second"});
    archive.Field("motorTorque", m_motorTorque,
                  {.min = 0.0f, .max = 1.0e9f, .speed = 1.0f, .tooltip = "The most it pushes with, in newton meters"});
}

void HingeJoint::Describe(Detail::PhysicsJointDesc& desc, const Transform& transform,
                          const Transform* /*connected*/) const {
    desc.kind = Detail::PhysicsJointKind::Hinge;
    desc.point = ToWorld(transform, GetAnchor());
    desc.axis = Normalize(transform.GetWorldRotation() * m_axis);
    desc.normal = GetPerpendicular(desc.axis);
    desc.limits = m_limits;
    desc.minAngle = m_minAngle;
    desc.maxAngle = m_maxAngle;
    desc.motor = m_motor;
    desc.motorSpeed = m_motorSpeed;
    desc.motorTorque = m_motorTorque;
}

void DistanceJoint::SetConnectedAnchor(Vector3 anchor) {
    m_connectedAnchor = anchor;
    MarkChanged();
}

void DistanceJoint::SetDistance(float minDistance, float maxDistance) {
    m_minDistance = minDistance;
    m_maxDistance = maxDistance;
    MarkChanged();
}

void DistanceJoint::Serialize(Archive& archive) {
    Joint::Serialize(archive);
    archive.Field("connectedAnchor", m_connectedAnchor,
                  {.speed = 0.01f, .tooltip = "Relative to the connected entity, or in the world without one"});
    archive.Field("minDistance", m_minDistance, {.min = 0.0f, .max = 1.0e6f, .speed = 0.01f});
    archive.Field("maxDistance", m_maxDistance,
                  {.min = -1.0f, .max = 1.0e6f, .speed = 0.01f, .tooltip = "Negative: the distance when it is made"});
}

void DistanceJoint::Describe(Detail::PhysicsJointDesc& desc, const Transform& transform,
                             const Transform* connected) const {
    desc.kind = Detail::PhysicsJointKind::Distance;
    desc.point = connected ? ToWorld(*connected, m_connectedAnchor) : m_connectedAnchor;
    desc.secondPoint = ToWorld(transform, GetAnchor());
    desc.minDistance = m_minDistance;
    desc.maxDistance = m_maxDistance;
}

void SpringJoint::SetConnectedAnchor(Vector3 anchor) {
    m_connectedAnchor = anchor;
    MarkChanged();
}

void SpringJoint::SetRestLength(float length) {
    m_restLength = length;
    MarkChanged();
}

void SpringJoint::SetFrequency(float hertz) {
    m_frequency = hertz;
    MarkChanged();
}

void SpringJoint::SetDamping(float damping) {
    m_damping = damping;
    MarkChanged();
}

void SpringJoint::Serialize(Archive& archive) {
    Joint::Serialize(archive);
    archive.Field("connectedAnchor", m_connectedAnchor,
                  {.speed = 0.01f, .tooltip = "Relative to the connected entity, or in the world without one"});
    archive.Field("restLength", m_restLength,
                  {.min = -1.0f, .max = 1.0e6f, .speed = 0.01f, .tooltip = "Negative: the distance when it is made"});
    archive.Field("frequency", m_frequency, {.min = 0.0f, .max = 100.0f, .speed = 0.01f, .tooltip = "In hertz"});
    archive.Field("damping", m_damping, {.min = 0.0f, .max = 10.0f, .speed = 0.01f});
}

void SpringJoint::Describe(Detail::PhysicsJointDesc& desc, const Transform& transform,
                           const Transform* connected) const {
    desc.kind = Detail::PhysicsJointKind::Distance;
    desc.point = connected ? ToWorld(*connected, m_connectedAnchor) : m_connectedAnchor;
    desc.secondPoint = ToWorld(transform, GetAnchor());
    desc.minDistance = m_restLength;
    desc.maxDistance = m_restLength;
    // A spring of no frequency would be a rigid rod, so it keeps a little.
    desc.springFrequency = std::max(m_frequency, 0.01f);
    desc.springDamping = m_damping;
}

void SwingTwistJoint::SetTwistAxis(Vector3 axis) {
    m_twistAxis = axis;
    MarkChanged();
}

void SwingTwistJoint::SetSwingLimit(float radians) {
    m_swingLimit = radians;
    MarkChanged();
}

void SwingTwistJoint::SetTwistLimits(float minAngle, float maxAngle) {
    m_twistMin = minAngle;
    m_twistMax = maxAngle;
    MarkChanged();
}

void SwingTwistJoint::Serialize(Archive& archive) {
    Joint::Serialize(archive);
    archive.Field("twistAxis", m_twistAxis,
                  {.speed = 0.01f, .tooltip = "Relative to the entity, usually along the bone"});
    archive.Field(
        "swingLimit", m_swingLimit,
        {.min = 0.0f, .max = 3.14159265f, .angle = true, .tooltip = "The half angle of the cone it swings in"});
    archive.Field("twistMin", m_twistMin, {.min = -3.14159265f, .max = 0.0f, .angle = true});
    archive.Field("twistMax", m_twistMax, {.min = 0.0f, .max = 3.14159265f, .angle = true});
}

void SwingTwistJoint::Describe(Detail::PhysicsJointDesc& desc, const Transform& transform,
                               const Transform* /*connected*/) const {
    desc.kind = Detail::PhysicsJointKind::SwingTwist;
    desc.point = ToWorld(transform, GetAnchor());
    desc.axis = Normalize(transform.GetWorldRotation() * m_twistAxis);
    desc.normal = GetPerpendicular(desc.axis);
    desc.swingLimit = m_swingLimit;
    desc.twistMin = m_twistMin;
    desc.twistMax = m_twistMax;
}

} // namespace Spot
