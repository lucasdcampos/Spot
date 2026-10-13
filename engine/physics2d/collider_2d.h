#pragma once

#include "physics2d/physics_2d.h"
#include "scene/component.h"

namespace Spot {
class Collider2D : public Component {
public:
    Vector2 GetCenter() const { return m_shape.center; }
    void SetCenter(Vector2 value) {
        m_shape.center = value;
        ++m_revision;
    }
    float GetFriction() const { return m_shape.friction; }
    void SetFriction(float value) {
        m_shape.friction = value;
        ++m_revision;
    }
    float GetRestitution() const { return m_shape.restitution; }
    void SetRestitution(float value) {
        m_shape.restitution = value;
        ++m_revision;
    }
    bool IsTrigger() const { return m_shape.trigger; }
    void SetTrigger(bool value) {
        m_shape.trigger = value;
        ++m_revision;
    }
    Shape2D GetShape(Vector2 scale) const;
    virtual std::uint64_t GetRevision() const { return m_revision; }
    virtual void AppendShapes(Vector2 scale, std::vector<Shape2D>& shapes) const;

protected:
    explicit Collider2D(ShapeKind2D kind) { m_shape.kind = kind; }
    void OnEnable() override;
    void OnValidate() override { ++m_revision; }
    void Serialize(Archive& archive) override;
    Shape2D m_shape;
    std::uint64_t m_revision = 1;
};
class BoxCollider2D final : public Collider2D {
public:
    explicit BoxCollider2D(Vector2 size = {1.0f, 1.0f}) : Collider2D(ShapeKind2D::Box) { m_shape.size = size; }
    Vector2 GetSize() const { return m_shape.size; }
    void SetSize(Vector2 value) {
        m_shape.size = value;
        ++m_revision;
    }

protected:
    void Serialize(Archive& archive) override;
};
class CircleCollider2D final : public Collider2D {
public:
    explicit CircleCollider2D(float radius = 0.5f) : Collider2D(ShapeKind2D::Circle) { m_shape.radius = radius; }
    float GetRadius() const { return m_shape.radius; }
    void SetRadius(float value) {
        m_shape.radius = value;
        ++m_revision;
    }

protected:
    void Serialize(Archive& archive) override;
};
class CapsuleCollider2D final : public Collider2D {
public:
    explicit CapsuleCollider2D(float radius = 0.5f, float height = 2.0f) : Collider2D(ShapeKind2D::Capsule) {
        m_shape.radius = radius;
        m_shape.height = height;
    }
    float GetRadius() const { return m_shape.radius; }
    void SetRadius(float value) {
        m_shape.radius = value;
        ++m_revision;
    }
    float GetHeight() const { return m_shape.height; }
    void SetHeight(float value) {
        m_shape.height = value;
        ++m_revision;
    }

protected:
    void Serialize(Archive& archive) override;
};
class PolygonCollider2D final : public Collider2D {
public:
    PolygonCollider2D() : Collider2D(ShapeKind2D::Polygon) {
        m_shape.vertices = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.5f}};
    }
    const std::vector<Vector2>& GetVertices() const { return m_shape.vertices; }
    void SetVertices(std::vector<Vector2> value) {
        m_shape.vertices = std::move(value);
        ++m_revision;
    }

protected:
    void Serialize(Archive& archive) override;
};
} // namespace Spot
