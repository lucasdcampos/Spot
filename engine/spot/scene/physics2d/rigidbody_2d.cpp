#include "spot/scene/physics2d/rigidbody_2d.h"
#include "spot/assets/asset_archive.h"
#include "spot/core/archive.h"
#include "spot/scene/physics2d/physics_world_2d.h"

namespace Spot {
void Rigidbody2D::OnEnable() {
    GetScene().GetPhysics2D();
}
Vector2 Rigidbody2D::GetLinearVelocity() const {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled()) {
        auto id = world->FindBody(GetEntity().GetId());
        if (id != BodyId2D::Invalid)
            return world->m_simulation.GetLinearVelocity(id);
    }
    return m_desc.linearVelocity;
}
float Rigidbody2D::GetAngularVelocity() const {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled()) {
        auto id = world->FindBody(GetEntity().GetId());
        if (id != BodyId2D::Invalid)
            return world->m_simulation.GetAngularVelocity(id);
    }
    return m_desc.angularVelocity;
}
void Rigidbody2D::SetLinearVelocity(Vector2 v) {
    m_desc.linearVelocity = v;
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.SetLinearVelocity(world->FindBody(GetEntity().GetId()), v);
}
void Rigidbody2D::SetAngularVelocity(float v) {
    m_desc.angularVelocity = v;
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.SetAngularVelocity(world->FindBody(GetEntity().GetId()), v);
}
void Rigidbody2D::AddForce(Vector2 force) {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.AddForce(world->FindBody(GetEntity().GetId()), force);
}
void Rigidbody2D::AddImpulse(Vector2 impulse) {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.AddImpulse(world->FindBody(GetEntity().GetId()), impulse);
    else if (m_desc.mass > 0.0f)
        m_desc.linearVelocity += impulse / m_desc.mass;
}
void Rigidbody2D::AddForceAtPosition(Vector2 force, Vector2 position) {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.AddForceAtPosition(world->FindBody(GetEntity().GetId()), force, position);
}
void Rigidbody2D::AddImpulseAtPosition(Vector2 impulse, Vector2 position) {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.AddImpulseAtPosition(world->FindBody(GetEntity().GetId()), impulse, position);
    else
        AddImpulse(impulse);
}
void Rigidbody2D::AddTorque(float torque) {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.AddTorque(world->FindBody(GetEntity().GetId()), torque);
}
bool Rigidbody2D::IsSleeping() const {
    auto* world = GetScene().GetSystem<PhysicsWorld2D>();
    return world && IsActiveAndEnabled() && !world->m_simulation.IsAwake(world->FindBody(GetEntity().GetId()));
}
void Rigidbody2D::WakeUp() {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.WakeUp(world->FindBody(GetEntity().GetId()));
}
void Rigidbody2D::Sleep() {
    if (auto* world = GetScene().GetSystem<PhysicsWorld2D>(); world && IsActiveAndEnabled())
        world->m_simulation.Sleep(world->FindBody(GetEntity().GetId()));
}
void Rigidbody2D::Serialize(Archive& archive) {
    bool kinematic = IsKinematic();
    archive.Field("kinematic", kinematic);
    if (archive.IsLoading())
        m_desc.motion = kinematic ? BodyMotion2D::Kinematic : BodyMotion2D::Dynamic;
    archive.Field("mass", m_desc.mass, {.min = 0.001f, .max = 100000.0f});
    archive.Field("gravityScale", m_desc.gravityScale);
    archive.Field("linearDamping", m_desc.linearDamping, {.min = 0.0f, .max = 100.0f});
    archive.Field("angularDamping", m_desc.angularDamping, {.min = 0.0f, .max = 100.0f});
    archive.Field("lockRotation", m_desc.lockRotation);
    archive.Field("continuous", m_desc.continuous);
    archive.Field("canSleep", m_desc.allowSleeping);
}
} // namespace Spot
