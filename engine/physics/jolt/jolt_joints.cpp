#include "physics/jolt/jolt_simulation_impl.h"

#include "core/log.h"

#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <algorithm>
#include <cmath>

namespace Spot::Detail {

namespace {

JPH::Ref<JPH::TwoBodyConstraintSettings> MakeSettings(const PhysicsJointDesc& desc) {
    const JPH::RVec3 point = ToJolt(desc.point);
    const JPH::Vec3 axis = ToJolt(desc.axis).NormalizedOr(JPH::Vec3::sAxisY());
    const JPH::Vec3 normal = ToJolt(desc.normal).NormalizedOr(axis.GetNormalizedPerpendicular());
    switch (desc.kind) {
    case PhysicsJointKind::Fixed: {
        auto* settings = new JPH::FixedConstraintSettings();
        settings->mAutoDetectPoint = true;
        return settings;
    }
    case PhysicsJointKind::Hinge: {
        auto* settings = new JPH::HingeConstraintSettings();
        settings->mPoint1 = settings->mPoint2 = point;
        settings->mHingeAxis1 = settings->mHingeAxis2 = axis;
        settings->mNormalAxis1 = settings->mNormalAxis2 = normal;
        if (desc.limits) {
            settings->mLimitsMin = std::clamp(desc.minAngle, -JPH::JPH_PI, 0.0f);
            settings->mLimitsMax = std::clamp(desc.maxAngle, 0.0f, JPH::JPH_PI);
        }
        settings->mMotorSettings = JPH::MotorSettings(0.0f, 0.0f, 0.0f, std::max(desc.motorTorque, 0.0f));
        return settings;
    }
    case PhysicsJointKind::Distance: {
        auto* settings = new JPH::DistanceConstraintSettings();
        settings->mPoint1 = point;
        settings->mPoint2 = ToJolt(desc.secondPoint);
        settings->mMinDistance = desc.minDistance;
        settings->mMaxDistance = desc.maxDistance;
        settings->mLimitsSpringSettings.mFrequency = std::max(desc.springFrequency, 0.0f);
        settings->mLimitsSpringSettings.mDamping = std::max(desc.springDamping, 0.0f);
        return settings;
    }
    case PhysicsJointKind::SwingTwist: {
        auto* settings = new JPH::SwingTwistConstraintSettings();
        settings->mPosition1 = settings->mPosition2 = point;
        settings->mTwistAxis1 = settings->mTwistAxis2 = axis;
        settings->mPlaneAxis1 = settings->mPlaneAxis2 = normal;
        const float swing = std::clamp(desc.swingLimit, 0.0f, JPH::JPH_PI);
        settings->mNormalHalfConeAngle = swing;
        settings->mPlaneHalfConeAngle = swing;
        settings->mTwistMinAngle = std::clamp(desc.twistMin, -JPH::JPH_PI, 0.0f);
        settings->mTwistMaxAngle = std::clamp(desc.twistMax, 0.0f, JPH::JPH_PI);
        return settings;
    }
    }
    return new JPH::FixedConstraintSettings();
}

} // namespace

PhysicsJointId PhysicsSimulation::CreateJoint(const PhysicsJointDesc& desc) {
    const JPH::Ref<JPH::TwoBodyConstraintSettings> settings = MakeSettings(desc);
    // An invalid id stands for the world, which Jolt joins bodies to as a fixed body of its own.
    const JPH::BodyID first = desc.first == PhysicsBodyId::Invalid ? JPH::BodyID() : ToJolt(desc.first);
    JPH::Ref<JPH::TwoBodyConstraint> joint = m_impl->GetBodies().CreateConstraint(settings, first, ToJolt(desc.second));
    if (!joint) {
        Log::GetEngineLogger().Error("Could not make a physics joint");
        return PhysicsJointId::Invalid;
    }
    if (desc.kind == PhysicsJointKind::Hinge && desc.motor) {
        auto& hinge = static_cast<JPH::HingeConstraint&>(*joint);
        hinge.SetMotorState(JPH::EMotorState::Velocity);
        hinge.SetTargetAngularVelocity(desc.motorSpeed);
    }
    m_impl->system.AddConstraint(joint);
    m_impl->GetBodies().ActivateConstraint(joint);

    std::size_t slot = 0;
    while (slot < m_impl->joints.size() && m_impl->joints[slot]) {
        ++slot;
    }
    if (slot == m_impl->joints.size()) {
        m_impl->joints.emplace_back();
    }
    m_impl->joints[slot] = std::move(joint);
    return static_cast<PhysicsJointId>(slot);
}

void PhysicsSimulation::DestroyJoint(PhysicsJointId id) {
    JPH::Ref<JPH::TwoBodyConstraint>& joint = m_impl->joints[static_cast<std::size_t>(id)];
    // What it held falls once it lets go.
    m_impl->GetBodies().ActivateConstraint(joint);
    m_impl->system.RemoveConstraint(joint);
    joint = nullptr;
}

float PhysicsSimulation::GetJointForce(PhysicsJointId id, float stepTime) const {
    const JPH::TwoBodyConstraint& joint = *m_impl->joints[static_cast<std::size_t>(id)];
    float impulse = 0.0f;
    switch (joint.GetSubType()) {
    case JPH::EConstraintSubType::Fixed:
        impulse = static_cast<const JPH::FixedConstraint&>(joint).GetTotalLambdaPosition().Length();
        break;
    case JPH::EConstraintSubType::Hinge:
        impulse = static_cast<const JPH::HingeConstraint&>(joint).GetTotalLambdaPosition().Length();
        break;
    case JPH::EConstraintSubType::Distance:
        impulse = std::abs(static_cast<const JPH::DistanceConstraint&>(joint).GetTotalLambdaPosition());
        break;
    case JPH::EConstraintSubType::SwingTwist:
        impulse = static_cast<const JPH::SwingTwistConstraint&>(joint).GetTotalLambdaPosition().Length();
        break;
    default:
        break;
    }
    return stepTime > 0.0f ? impulse / stepTime : 0.0f;
}

float PhysicsSimulation::GetHingeAngle(PhysicsJointId id) const {
    const JPH::TwoBodyConstraint& joint = *m_impl->joints[static_cast<std::size_t>(id)];
    return joint.GetSubType() == JPH::EConstraintSubType::Hinge
               ? static_cast<const JPH::HingeConstraint&>(joint).GetCurrentAngle()
               : 0.0f;
}

void PhysicsSimulation::SetHingeMotor(PhysicsJointId id, bool enabled, float speed, float torque) {
    JPH::TwoBodyConstraint& joint = *m_impl->joints[static_cast<std::size_t>(id)];
    if (joint.GetSubType() != JPH::EConstraintSubType::Hinge) {
        return;
    }
    auto& hinge = static_cast<JPH::HingeConstraint&>(joint);
    hinge.GetMotorSettings().SetTorqueLimit(std::max(torque, 0.0f));
    hinge.SetMotorState(enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
    hinge.SetTargetAngularVelocity(speed);
    m_impl->GetBodies().ActivateConstraint(&hinge);
}

} // namespace Spot::Detail
