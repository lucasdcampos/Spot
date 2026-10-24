#include "spot/physics/jolt/jolt_shapes.h"
#include "spot/physics/jolt/jolt_simulation_impl.h"

#include "spot/core/log.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <algorithm>

namespace Spot {

using namespace Detail;

Physics3D::Impl::Impl() {
    system.Init(MaxBodies, 0, MaxBodyPairs, MaxContactConstraints, broadPhaseLayers, objectVsBroadPhaseFilter,
                objectPairFilter);
    system.SetContactListener(&contacts);
}

void Physics3D::Impl::WakeBodiesAround(JPH::BodyID body) {
    JPH::AABox bounds = GetBodies().GetTransformedShape(body).GetWorldSpaceBounds();
    // Grown a little, to reach bodies that rest on it without overlapping.
    bounds.ExpandBy(JPH::Vec3::sReplicate(0.1f));
    GetBodies().ActivateBodiesInAABox(bounds, {}, {});
}

Physics3D::Physics3D() : m_impl(std::make_unique<Impl>()) {
}

Physics3D::~Physics3D() = default;

void Physics3D::SetLayerMasks(const std::array<std::uint32_t, 32>& masks) {
    m_impl->layerMasks = masks;
}

void Physics3D::SetGravity(Vector3 gravity) {
    m_impl->system.SetGravity(ToJolt(gravity));
    // Resting bodies would otherwise ignore the new gravity until something touches them.
    JPH::BodyIDVector bodies;
    m_impl->system.GetBodies(bodies);
    m_impl->GetBodies().ActivateBodies(bodies.data(), static_cast<int>(bodies.size()));
}

BodyId3D Physics3D::CreateBody(const BodyDesc3D& desc) {
    const bool isStatic = desc.motion == BodyMotion3D::Static;
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
    if (desc.motion == BodyMotion3D::Dynamic) {
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
    if (desc.motion == BodyMotion3D::Dynamic) {
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
        return BodyId3D::Invalid;
    }
    m_impl->GetBodies().AddBody(body->GetID(), isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    return FromJolt(body->GetID());
}

void Physics3D::UpdateBody(BodyId3D body, const BodyDesc3D& desc) {
    const JPH::BodyID id = ToJolt(body);
    JPH::BodyInterface& bodies = m_impl->GetBodies();
    const bool isStatic = desc.motion == BodyMotion3D::Static;
    bodies.SetFriction(id, desc.friction);
    bodies.SetRestitution(id, desc.restitution);
    bodies.SetObjectLayer(id, ToObjectLayer(desc.layer, !isStatic));
    if (isStatic) {
        return;
    }
    bodies.SetGravityFactor(id, desc.gravityScale);
    bodies.SetMotionQuality(id, desc.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete);
    {
        const JPH::BodyLockWrite lock(m_impl->system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) {
            return;
        }
        JPH::Body& locked = lock.GetBody();
        JPH::MotionProperties* motion = locked.GetMotionProperties();
        motion->SetLinearDamping(desc.linearDamping);
        motion->SetAngularDamping(desc.angularDamping);
        locked.SetAllowSleeping(desc.allowSleeping);
        if (desc.motion == BodyMotion3D::Dynamic) {
            auto dofs = static_cast<std::uint8_t>(JPH::EAllowedDOFs::All);
            dofs &= desc.lockRotationX ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationX) : 0xff;
            dofs &= desc.lockRotationY ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationY) : 0xff;
            dofs &= desc.lockRotationZ ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationZ) : 0xff;
            const float mass = std::max(desc.mass, MinShapeSize);
            JPH::MassProperties properties;
            if (desc.shapes.empty()) {
                // As when created: without shapes, the body turns like a ball of size 1.
                properties.mMass = mass;
                properties.mInertia = JPH::Mat44::sScale(0.1f * mass);
            } else {
                properties = locked.GetShape()->GetMassProperties();
                properties.ScaleToMass(mass);
            }
            motion->SetMassProperties(static_cast<JPH::EAllowedDOFs>(dofs), properties);
        }
    }
    bodies.ActivateBody(id);
}

void Physics3D::DestroyBody(BodyId3D body) {
    const JPH::BodyID id = ToJolt(body);
    m_impl->WakeBodiesAround(id);
    m_impl->GetBodies().RemoveBody(id);
    m_impl->GetBodies().DestroyBody(id);
    m_impl->contacts.ForgetBody(id);
}

void Physics3D::Step(float deltaTime) {
    m_impl->contacts.BeginStep();
    JoltRuntime& runtime = m_impl->runtime.Get();
    const JPH::EPhysicsUpdateError error =
        m_impl->system.Update(deltaTime, 1, &runtime.tempAllocator, &runtime.jobSystem);
    m_impl->contacts.EndStep(m_impl->GetBodies());
    if (error != JPH::EPhysicsUpdateError::None) {
        Log::GetEngineLogger().Warning("The physics step ran out of room for contacts; some collisions were missed");
    }
}

void Physics3D::GetContacts(std::vector<Contact3D>& contacts) const {
    m_impl->contacts.GetContacts(contacts);
}

Pose3D Physics3D::GetPose(BodyId3D body) const {
    JPH::RVec3 position;
    JPH::Quat rotation;
    m_impl->GetBodies().GetPositionAndRotation(ToJolt(body), position, rotation);
    return {.position = FromJolt(position), .rotation = FromJolt(rotation)};
}

void Physics3D::SetPose(BodyId3D body, Pose3D pose) {
    const JPH::BodyID id = ToJolt(body);
    m_impl->WakeBodiesAround(id);
    m_impl->GetBodies().SetPositionAndRotation(id, ToJolt(pose.position), ToJolt(pose.rotation),
                                               JPH::EActivation::Activate);
    m_impl->WakeBodiesAround(id);
}

void Physics3D::MoveKinematic(BodyId3D body, Pose3D pose, float deltaTime) {
    m_impl->GetBodies().MoveKinematic(ToJolt(body), ToJolt(pose.position), ToJolt(pose.rotation), deltaTime);
}

bool Physics3D::IsAwake(BodyId3D body) const {
    return m_impl->GetBodies().IsActive(ToJolt(body));
}

Vector3 Physics3D::GetLinearVelocity(BodyId3D body) const {
    return FromJolt(m_impl->GetBodies().GetLinearVelocity(ToJolt(body)));
}

void Physics3D::SetLinearVelocity(BodyId3D body, Vector3 velocity) {
    m_impl->GetBodies().SetLinearVelocity(ToJolt(body), ToJolt(velocity));
}

Vector3 Physics3D::GetAngularVelocity(BodyId3D body) const {
    return FromJolt(m_impl->GetBodies().GetAngularVelocity(ToJolt(body)));
}

void Physics3D::SetAngularVelocity(BodyId3D body, Vector3 velocity) {
    m_impl->GetBodies().SetAngularVelocity(ToJolt(body), ToJolt(velocity));
}

void Physics3D::AddForce(BodyId3D body, Vector3 force) {
    m_impl->GetBodies().AddForce(ToJolt(body), ToJolt(force));
}

void Physics3D::AddImpulse(BodyId3D body, Vector3 impulse) {
    m_impl->GetBodies().AddImpulse(ToJolt(body), ToJolt(impulse));
}

void Physics3D::AddForceAtPosition(BodyId3D body, Vector3 force, Vector3 position) {
    m_impl->GetBodies().AddForce(ToJolt(body), ToJolt(force), ToJolt(position));
}

void Physics3D::AddImpulseAtPosition(BodyId3D body, Vector3 impulse, Vector3 position) {
    m_impl->GetBodies().AddImpulse(ToJolt(body), ToJolt(impulse), ToJolt(position));
}

void Physics3D::AddTorque(BodyId3D body, Vector3 torque) {
    m_impl->GetBodies().AddTorque(ToJolt(body), ToJolt(torque));
}

void Physics3D::WakeUp(BodyId3D body) {
    m_impl->GetBodies().ActivateBody(ToJolt(body));
}

void Physics3D::Sleep(BodyId3D body) {
    m_impl->GetBodies().DeactivateBody(ToJolt(body));
}

} // namespace Spot
