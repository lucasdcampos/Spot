#include "scene/physics/rigidbody.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
#include "scene/physics/physics_world.h"
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

void Rigidbody::AddForceAtPosition(Vector3 force, Vector3 position) {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId());
        body && body->motion == PhysicsMotion::Dynamic) {
        physics.GetSimulation().AddForceAtPosition(body->id, force, position);
    }
}

void Rigidbody::AddImpulseAtPosition(Vector3 impulse, Vector3 position) {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId())) {
        if (body->motion == PhysicsMotion::Dynamic) {
            physics.GetSimulation().AddImpulseAtPosition(body->id, impulse, position);
        }
    } else if (!m_kinematic && m_mass > 0.0f) {
        m_linearVelocity += impulse / m_mass;
    }
}

void Rigidbody::AddTorque(Vector3 torque) {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId());
        body && body->motion == PhysicsMotion::Dynamic) {
        physics.GetSimulation().AddTorque(body->id, torque);
    }
}

void Rigidbody::SetRotationLocks(RotationLocks locks) {
    m_rotationLocks = locks;
    MarkChanged();
}

void Rigidbody::SetContinuous(bool continuous) {
    m_continuous = continuous;
    MarkChanged();
}

bool Rigidbody::IsSleeping() const {
    PhysicsWorld& physics = GetScene().GetPhysics();
    const auto* body = physics.FindMovingBody(GetEntity().GetId());
    return body && !physics.GetSimulation().IsAwake(body->id);
}

void Rigidbody::WakeUp() {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId())) {
        physics.GetSimulation().WakeUp(body->id);
    }
}

void Rigidbody::Sleep() {
    PhysicsWorld& physics = GetScene().GetPhysics();
    if (const auto* body = physics.FindMovingBody(GetEntity().GetId())) {
        physics.GetSimulation().Sleep(body->id);
    }
}

void Rigidbody::SetCanSleep(bool canSleep) {
    m_canSleep = canSleep;
    MarkChanged();
}

void RotationLocks::Serialize(Archive& archive) {
    archive.Field("x", x);
    archive.Field("y", y);
    archive.Field("z", z);
}

void Rigidbody::OnEnable() {
    MarkChanged();
}

void Rigidbody::OnDisable() {
    MarkChanged();
}

void Rigidbody::Serialize(Archive& archive) {
    archive.Field("mass", m_mass, {.min = 0.001f, .max = 100000.0f, .speed = 0.05f, .tooltip = "In kilograms"});
    archive.Field("kinematic", m_kinematic,
                  {.tooltip = "Moved by its transform alone, pushing others without being pushed"});
    archive.Field("gravityScale", m_gravityScale, {.min = -10.0f, .max = 10.0f, .speed = 0.01f});
    archive.Field("linearDamping", m_linearDamping, {.min = 0.0f, .max = 100.0f, .speed = 0.01f});
    archive.Field("angularDamping", m_angularDamping, {.min = 0.0f, .max = 100.0f, .speed = 0.01f});
    archive.Field("rotationLocks", m_rotationLocks, {.tooltip = "Rotations it cannot make, around the world's axes"});
    archive.Field("continuous", m_continuous, {.tooltip = "Keeps a fast body from passing through thin ones"});
    archive.Field("canSleep", m_canSleep, {.tooltip = "Whether it falls asleep once at rest"});
    Vector3 linearVelocity = GetLinearVelocity();
    Vector3 angularVelocity = GetAngularVelocity();
    archive.Field("linearVelocity", linearVelocity);
    archive.Field("angularVelocity", angularVelocity);
    if (archive.IsLoading()) {
        if (linearVelocity != GetLinearVelocity()) {
            SetLinearVelocity(linearVelocity);
        }
        if (angularVelocity != GetAngularVelocity()) {
            SetAngularVelocity(angularVelocity);
        }
    }
}

void Rigidbody::MarkChanged() {
    GetScene().GetPhysics().MarkChanged(GetEntity().GetId());
}

} // namespace Spot
