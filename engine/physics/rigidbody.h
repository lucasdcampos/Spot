#pragma once

#include "math/vector.h"
#include "scene/component.h"

namespace Spot {

// Lets physics move its entity: the body made of the entity's colliders becomes dynamic, falling with gravity and
// pushed by forces and collisions. A kinematic rigidbody is moved through its entity's transform instead, and pushes
// dynamic bodies out of its way without being pushed back. A rigidbody without colliders still falls, but collides
// with nothing. See PhysicsWorld.
//
//     auto& ball = scene.CreateEntity("Ball");
//     ball.AddComponent<Spot::SphereCollider>();
//     ball.AddComponent<Spot::Rigidbody>().SetMass(2.0f);
//
// Velocities, forces and impulses act on the body once it exists, from the first physics step after the rigidbody
// starts or as soon as one of them is used after that. Before, velocities set and impulses added become the body's
// starting velocity, and forces are ignored.
class Rigidbody final : public Component {
public:
    Rigidbody() = default;

    // In kilograms; dynamic bodies only.
    float GetMass() const { return m_mass; }
    void SetMass(float mass);

    bool IsKinematic() const { return m_kinematic; }
    void SetKinematic(bool kinematic);

    // How much gravity pulls the body: 0 makes it float, and 2 fall twice as fast.
    float GetGravityScale() const { return m_gravityScale; }
    void SetGravityScale(float scale);

    // How fast the body slows down by itself, like air resistance, per second.
    float GetLinearDamping() const { return m_linearDamping; }
    void SetLinearDamping(float damping);
    float GetAngularDamping() const { return m_angularDamping; }
    void SetAngularDamping(float damping);

    // In units per second.
    Vector3 GetLinearVelocity() const;
    void SetLinearVelocity(Vector3 velocity);
    // Around each axis, in radians per second.
    Vector3 GetAngularVelocity() const;
    void SetAngularVelocity(Vector3 velocity);

    // Dynamic bodies only. A force acts during the next physics step only, so steady pushes are applied in every
    // Component::OnFixedUpdate(). An impulse changes the velocity at once, by the impulse divided by the mass, as for
    // a jump or a hit.
    void AddForce(Vector3 force);
    void AddImpulse(Vector3 impulse);

protected:
    void OnEnable() override;
    void OnDisable() override;
    void Serialize(Archive& archive) override;

private:
    friend class PhysicsWorld;

    void MarkChanged();

    float m_mass = 1.0f;
    bool m_kinematic = false;
    float m_gravityScale = 1.0f;
    float m_linearDamping = 0.0f;
    float m_angularDamping = 0.05f;
    // The starting velocities of the next body, taken by it when it is created.
    Vector3 m_linearVelocity = {};
    Vector3 m_angularVelocity = {};
};

} // namespace Spot
