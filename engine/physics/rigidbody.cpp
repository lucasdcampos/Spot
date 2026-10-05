#include "physics/rigidbody.h"

#include "physics/physics_world.h"
#include "scene/archive.h"
#include "scene/scene.h"

namespace Spot {

using Detail::PhysicsMotion;

void Rigidbody::SetMass(float mass) {
    m_mass = mass;
    MarkChanged();
}

void Rigidbody::SetKinematic(bool kinematic) {
    m_kinematic = kinematic;
    MarkChanged();
}

void Rigidbody::SetGravityScale(float scale) {
    m_gravityScale = scale;
    MarkChanged();
}

void Rigidbody::SetLinearDamping(float damping) {
    m_linearDamping = damping;
    MarkChanged();
}

void Rigidbody::SetAngularDamping(float damping) {
    m_angularDamping = damping;
    MarkChanged();
}

Vector3 Rigidbody::GetLinearVelocity() const {
    PhysicsWorld& physics = GetScene().GetPhysics();
    const auto* body = physics.FindMovingBody(GetEntity().GetId());
    return body ? physics.GetSimulation().GetLinearVelocity(body->id) : m_linearVelocity;
}

void Rigidbody::SetLinearVelocity(Vector3 velocity) {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId())) {
        physics.GetSimulation().SetLinearVelocity(body->id, velocity);
    } else {
        m_linearVelocity = velocity;
    }
}

Vector3 Rigidbody::GetAngularVelocity() const {
    PhysicsWorld& physics = GetScene().GetPhysics();
    const auto* body = physics.FindMovingBody(GetEntity().GetId());
    return body ? physics.GetSimulation().GetAngularVelocity(body->id) : m_angularVelocity;
}

void Rigidbody::SetAngularVelocity(Vector3 velocity) {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId())) {
        physics.GetSimulation().SetAngularVelocity(body->id, velocity);
    } else {
        m_angularVelocity = velocity;
    }
}

void Rigidbody::AddForce(Vector3 force) {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId());
        body && body->motion == PhysicsMotion::Dynamic) {
        physics.GetSimulation().AddForce(body->id, force);
    }
}

void Rigidbody::AddImpulse(Vector3 impulse) {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId())) {
        if (body->motion == PhysicsMotion::Dynamic) {
            physics.GetSimulation().AddImpulse(body->id, impulse);
        }
    } else if (!m_kinematic && m_mass > 0.0f) {
        m_linearVelocity += impulse / m_mass;
    }
}

void Rigidbody::OnEnable() {
    MarkChanged();
}

void Rigidbody::OnDisable() {
    MarkChanged();
}

void Rigidbody::Serialize(Archive& archive) {
    archive.Field("mass", m_mass);
    archive.Field("kinematic", m_kinematic);
    archive.Field("gravityScale", m_gravityScale);
    archive.Field("linearDamping", m_linearDamping);
    archive.Field("angularDamping", m_angularDamping);
}

void Rigidbody::MarkChanged() {
    GetScene().GetPhysics().MarkChanged(GetEntity().GetId());
}

} // namespace Spot
