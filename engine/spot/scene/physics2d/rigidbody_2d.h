#pragma once
#include "spot/physics2d/physics_2d.h"
#include "spot/scene/component.h"

namespace Spot {
class Rigidbody2D final : public Component {
public:
    float GetMass() const { return m_desc.mass; }
    void SetMass(float value) {
        m_desc.mass = value;
        ++m_revision;
    }
    bool IsKinematic() const { return m_desc.motion == BodyMotion2D::Kinematic; }
    void SetKinematic(bool value) {
        m_desc.motion = value ? BodyMotion2D::Kinematic : BodyMotion2D::Dynamic;
        ++m_revision;
    }
    float GetGravityScale() const { return m_desc.gravityScale; }
    void SetGravityScale(float value) {
        m_desc.gravityScale = value;
        ++m_revision;
    }
    float GetLinearDamping() const { return m_desc.linearDamping; }
    void SetLinearDamping(float value) {
        m_desc.linearDamping = value;
        ++m_revision;
    }
    float GetAngularDamping() const { return m_desc.angularDamping; }
    void SetAngularDamping(float value) {
        m_desc.angularDamping = value;
        ++m_revision;
    }
    bool IsRotationLocked() const { return m_desc.lockRotation; }
    void SetRotationLocked(bool value) {
        m_desc.lockRotation = value;
        ++m_revision;
    }
    bool IsContinuous() const { return m_desc.continuous; }
    void SetContinuous(bool value) {
        m_desc.continuous = value;
        ++m_revision;
    }
    bool CanSleep() const { return m_desc.allowSleeping; }
    void SetCanSleep(bool value) {
        m_desc.allowSleeping = value;
        ++m_revision;
    }
    Vector2 GetLinearVelocity() const;
    void SetLinearVelocity(Vector2 velocity);
    float GetAngularVelocity() const;
    void SetAngularVelocity(float velocity);
    void AddForce(Vector2 force);
    void AddImpulse(Vector2 impulse);
    void AddForceAtPosition(Vector2 force, Vector2 position);
    void AddImpulseAtPosition(Vector2 impulse, Vector2 position);
    void AddTorque(float torque);
    bool IsSleeping() const;
    void WakeUp();
    void Sleep();

protected:
    void OnEnable() override;
    void OnValidate() override { ++m_revision; }
    void Serialize(Archive& archive) override;

private:
    friend class PhysicsWorld2D;
    BodyDesc2D m_desc{.motion = BodyMotion2D::Dynamic};
    std::uint64_t m_revision = 1;
};
} // namespace Spot
