#include "physics/jolt/jolt_shapes.h"
#include "physics/jolt/jolt_simulation_impl.h"

#include "core/log.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <algorithm>

namespace Spot::Detail {

PhysicsSimulation::Impl::Impl() {
    system.Init(MaxBodies, 0, MaxBodyPairs, MaxContactConstraints, broadPhaseLayers, objectVsBroadPhaseFilter,
                objectPairFilter);
    system.SetContactListener(&contacts);
}

void PhysicsSimulation::Impl::WakeBodiesAround(JPH::BodyID body) {
    JPH::AABox bounds = GetBodies().GetTransformedShape(body).GetWorldSpaceBounds();
    // Grown a little, to reach bodies that rest on it without overlapping.
    bounds.ExpandBy(JPH::Vec3::sReplicate(0.1f));
    GetBodies().ActivateBodiesInAABox(bounds, {}, {});
}

std::unique_ptr<PhysicsSimulation> PhysicsSimulation::Create() {
    return std::unique_ptr<PhysicsSimulation>(new PhysicsSimulation(std::make_unique<Impl>()));
}

PhysicsSimulation::PhysicsSimulation(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

PhysicsSimulation::~PhysicsSimulation() = default;

void PhysicsSimulation::SetLayerMasks(const std::array<std::uint32_t, 32>& masks) {
    m_impl->layerMasks = masks;
}

void PhysicsSimulation::SetGravity(Vector3 gravity) {
    m_impl->system.SetGravity(ToJolt(gravity));
    // Resting bodies would otherwise ignore the new gravity until something touches them.
    JPH::BodyIDVector bodies;
    m_impl->system.GetBodies(bodies);
    m_impl->GetBodies().ActivateBodies(bodies.data(), static_cast<int>(bodies.size()));
}

PhysicsBodyId PhysicsSimulation::CreateBody(const PhysicsBodyDesc& desc) {
    const bool isStatic = desc.motion == PhysicsMotion::Static;
    JPH::BodyCreationSettings settings(CreateBodyShape(desc.shapes, m_impl->meshShapes), ToJolt(desc.position),
                                       ToJolt(desc.rotation), ToJolt(desc.motion),
                                       ToObjectLayer(desc.layer, !isStatic));
    settings.mIsSensor = desc.sensor;
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mGravityFactor = desc.gravityScale;
    settings.mLinearDamping = desc.linearDamping;
    settings.mAngularDamping = desc.angularDamping;
    settings.mMotionQuality = desc.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    settings.mAllowSleeping = desc.allowSleeping;
    if (desc.motion == PhysicsMotion::Dynamic) {
        auto dofs = static_cast<std::uint8_t>(JPH::EAllowedDOFs::All);
        dofs &= desc.lockRotationX ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationX) : 0xff;
        dofs &= desc.lockRotationY ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationY) : 0xff;
        dofs &= desc.lockRotationZ ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationZ) : 0xff;
        settings.mAllowedDOFs = static_cast<JPH::EAllowedDOFs>(dofs);
    }
    // Jolt asserts that a new body starts within its speed limits, which a body at its limit can cross by rounding
    // when rebuilt.
    settings.mLinearVelocity = ClampLength(ToJolt(desc.linearVelocity), settings.mMaxLinearVelocity);
    settings.mAngularVelocity = ClampLength(ToJolt(desc.angularVelocity), settings.mMaxAngularVelocity);
    if (desc.motion == PhysicsMotion::Dynamic) {
        const float mass = std::max(desc.mass, MinShapeSize);
        settings.mMassPropertiesOverride.mMass = mass;
        if (desc.shapes.empty()) {
            // Without shapes there is nothing to compute the inertia from, so the body turns like a ball of size 1.
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(0.1f * mass);
        } else {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        }
    }

    JPH::Body* body = m_impl->GetBodies().CreateBody(settings);
    if (!body) {
        Log::GetEngineLogger().Error("Could not create a physics body: the limit of {} bodies is reached", MaxBodies);
        return PhysicsBodyId::Invalid;
    }
    m_impl->GetBodies().AddBody(body->GetID(), isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    return FromJolt(body->GetID());
}

void PhysicsSimulation::DestroyBody(PhysicsBodyId body) {
    const JPH::BodyID id = ToJolt(body);
    m_impl->WakeBodiesAround(id);
    m_impl->GetBodies().RemoveBody(id);
    m_impl->GetBodies().DestroyBody(id);
    m_impl->contacts.ForgetBody(id);
}

void PhysicsSimulation::Step(float deltaTime) {
    m_impl->contacts.BeginStep();
    JoltRuntime& runtime = m_impl->runtime.Get();
    const JPH::EPhysicsUpdateError error =
        m_impl->system.Update(deltaTime, 1, &runtime.tempAllocator, &runtime.jobSystem);
    m_impl->contacts.EndStep(m_impl->GetBodies());
    if (error != JPH::EPhysicsUpdateError::None) {
        Log::GetEngineLogger().Warning("The physics step ran out of room for contacts; some collisions were missed");
    }
}

void PhysicsSimulation::GetContacts(std::vector<PhysicsContact>& contacts) const {
    m_impl->contacts.GetContacts(contacts);
}

PhysicsPose PhysicsSimulation::GetPose(PhysicsBodyId body) const {
    JPH::RVec3 position;
    JPH::Quat rotation;
    m_impl->GetBodies().GetPositionAndRotation(ToJolt(body), position, rotation);
    return {.position = FromJolt(position), .rotation = FromJolt(rotation)};
}

void PhysicsSimulation::SetPose(PhysicsBodyId body, Vector3 position, const Quaternion& rotation) {
    const JPH::BodyID id = ToJolt(body);
    m_impl->WakeBodiesAround(id);
    m_impl->GetBodies().SetPositionAndRotation(id, ToJolt(position), ToJolt(rotation), JPH::EActivation::Activate);
    m_impl->WakeBodiesAround(id);
}

void PhysicsSimulation::MoveKinematic(PhysicsBodyId body, Vector3 position, const Quaternion& rotation,
                                      float deltaTime) {
    m_impl->GetBodies().MoveKinematic(ToJolt(body), ToJolt(position), ToJolt(rotation), deltaTime);
}

bool PhysicsSimulation::IsAwake(PhysicsBodyId body) const {
    return m_impl->GetBodies().IsActive(ToJolt(body));
}

Vector3 PhysicsSimulation::GetLinearVelocity(PhysicsBodyId body) const {
    return FromJolt(m_impl->GetBodies().GetLinearVelocity(ToJolt(body)));
}

void PhysicsSimulation::SetLinearVelocity(PhysicsBodyId body, Vector3 velocity) {
    m_impl->GetBodies().SetLinearVelocity(ToJolt(body), ToJolt(velocity));
}

Vector3 PhysicsSimulation::GetAngularVelocity(PhysicsBodyId body) const {
    return FromJolt(m_impl->GetBodies().GetAngularVelocity(ToJolt(body)));
}

void PhysicsSimulation::SetAngularVelocity(PhysicsBodyId body, Vector3 velocity) {
    m_impl->GetBodies().SetAngularVelocity(ToJolt(body), ToJolt(velocity));
}

void PhysicsSimulation::AddForce(PhysicsBodyId body, Vector3 force) {
    m_impl->GetBodies().AddForce(ToJolt(body), ToJolt(force));
}

void PhysicsSimulation::AddImpulse(PhysicsBodyId body, Vector3 impulse) {
    m_impl->GetBodies().AddImpulse(ToJolt(body), ToJolt(impulse));
}

void PhysicsSimulation::AddForceAtPosition(PhysicsBodyId body, Vector3 force, Vector3 position) {
    m_impl->GetBodies().AddForce(ToJolt(body), ToJolt(force), ToJolt(position));
}

void PhysicsSimulation::AddImpulseAtPosition(PhysicsBodyId body, Vector3 impulse, Vector3 position) {
    m_impl->GetBodies().AddImpulse(ToJolt(body), ToJolt(impulse), ToJolt(position));
}

void PhysicsSimulation::AddTorque(PhysicsBodyId body, Vector3 torque) {
    m_impl->GetBodies().AddTorque(ToJolt(body), ToJolt(torque));
}

void PhysicsSimulation::WakeUp(PhysicsBodyId body) {
    m_impl->GetBodies().ActivateBody(ToJolt(body));
}

void PhysicsSimulation::Sleep(PhysicsBodyId body) {
    m_impl->GetBodies().DeactivateBody(ToJolt(body));
}

} // namespace Spot::Detail
