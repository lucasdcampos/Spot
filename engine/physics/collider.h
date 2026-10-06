#pragma once

#include "math/vector.h"
#include "physics/physics_simulation.h"
#include "scene/component.h"

namespace Spot {

// Gives its entity a solid shape. The active colliders of an entity make up a single body with the entity's world
// pose, which only moves by itself when the entity also has a Rigidbody; see PhysicsWorld. Shapes are scaled with the
// entity.
class Collider : public Component {
public:
    // Where the shape is centered, relative to the entity and before its scale.
    Vector3 GetCenter() const { return m_center; }
    void SetCenter(Vector3 center);

    // A body takes its friction and bounciness from its first collider. Friction goes from 0, for ice, to 1 and
    // beyond, for rubber; a restitution of 1 bounces back with all the speed it hit with.
    float GetFriction() const { return m_friction; }
    void SetFriction(float friction);
    float GetRestitution() const { return m_restitution; }
    void SetRestitution(float restitution);

protected:
    Collider() = default;

    void OnEnable() override;
    void OnDisable() override;
    void Serialize(Archive& archive) override;
    void OnValidate() override { MarkChanged(); }

    // Rebuilds the entity's body with the collider's new values.
    void MarkChanged();

private:
    friend class PhysicsWorld;

    // The shape for an entity with this world scale.
    virtual Detail::PhysicsShape GetShape(Vector3 scale) const = 0;

    Vector3 m_center = {};
    float m_friction = 0.5f;
    float m_restitution = 0.0f;
};

// A box, of size 1 by default like the built-in cube mesh.
class BoxCollider final : public Collider {
public:
    explicit BoxCollider(Vector3 size = {1.0f, 1.0f, 1.0f}) : m_size(size) {}

    Vector3 GetSize() const { return m_size; }
    void SetSize(Vector3 size);

protected:
    void Serialize(Archive& archive) override;

private:
    Detail::PhysicsShape GetShape(Vector3 scale) const override;

    Vector3 m_size;
};

// A sphere, of diameter 1 by default like the built-in sphere mesh. It stays round on entities scaled unevenly, taking
// the largest scale.
class SphereCollider final : public Collider {
public:
    explicit SphereCollider(float radius = 0.5f) : m_radius(radius) {}

    float GetRadius() const { return m_radius; }
    void SetRadius(float radius);

protected:
    void Serialize(Archive& archive) override;

private:
    Detail::PhysicsShape GetShape(Vector3 scale) const override;

    float m_radius;
};

// A capsule standing along Y, which suits characters since it slides over steps and edges. Its height includes both
// caps, so a capsule no taller than its diameter is a sphere. Its radius takes the largest of the X and Z scales.
class CapsuleCollider final : public Collider {
public:
    explicit CapsuleCollider(float radius = 0.5f, float height = 2.0f) : m_radius(radius), m_height(height) {}

    float GetRadius() const { return m_radius; }
    void SetRadius(float radius);
    float GetHeight() const { return m_height; }
    void SetHeight(float height);

protected:
    void Serialize(Archive& archive) override;

private:
    Detail::PhysicsShape GetShape(Vector3 scale) const override;

    float m_radius;
    float m_height;
};

} // namespace Spot
