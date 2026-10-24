#pragma once

#include "spot/core/math/matrix.h"
#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"

#include <cstdint>

namespace Spot {

class Entity;

// Where an entity is. Position, rotation and scale are relative to the parent, or to the world for entities without
// one; scale applies first, then rotation, then position. In 2D, x and y place the entity and the rotation turns
// around Z.
//
// World values are cached and only recomputed after this transform or one of its ancestors changes.
class Transform {
public:
    Transform(const Transform&) = delete;
    Transform& operator=(const Transform&) = delete;

    Entity& GetEntity() const { return m_entity; }

    Vector3 GetPosition() const { return m_position; }
    void SetPosition(Vector3 position);
    void Translate(Vector3 offset) { SetPosition(m_position + offset); }

    const Quaternion& GetRotation() const { return m_rotation; }
    void SetRotation(const Quaternion& rotation);
    // Applies the rotation after the current one.
    void Rotate(const Quaternion& rotation) { SetRotation(rotation * m_rotation); }
    // Sets the rotation to an angle around Z, in radians, counterclockwise.
    void SetRotation2D(float angle);
    // The angle around Z; only meaningful when the rotation is around Z alone.
    float GetRotation2D() const;

    Vector3 GetScale() const { return m_scale; }
    void SetScale(Vector3 scale);

    Matrix4 GetLocalMatrix() const;
    // System-owned world poses leave the authored local fields unchanged.
    void SetWorldOverride(const Matrix4& matrix);
    void ClearWorldOverride();
    bool HasWorldOverride() const { return m_hasWorldOverride; }

    const Matrix4& GetWorldMatrix() const;
    Vector3 GetWorldPosition() const;
    void SetWorldPosition(Vector3 position);
    // Exact when the ancestors are scaled uniformly.
    const Quaternion& GetWorldRotation() const;
    void SetWorldRotation(const Quaternion& rotation);
    float GetWorldRotation2D() const;
    // Changes whenever the world values may have, this transform's or an ancestor's change included, so systems
    // mirroring transforms elsewhere update only those that moved.
    std::uint64_t GetVersion() const { return m_version; }

private:
    friend class Entity;
    friend class Scene;

    explicit Transform(Entity& entity) : m_entity(entity) {}

    // Marks the world values of this transform and its descendants as outdated. A transform with outdated world
    // values always has descendants with outdated ones too, since computing theirs computes its own first.
    void MarkWorldDirty();
    void UpdateWorld() const;

    Entity& m_entity;
    Vector3 m_position = {};
    Quaternion m_rotation = {};
    Vector3 m_scale = {1.0f, 1.0f, 1.0f};

    Matrix4 m_worldOverride;
    bool m_hasWorldOverride = false;
    mutable Matrix4 m_worldMatrix = {};
    mutable Quaternion m_worldRotation = {};
    mutable bool m_worldDirty = true;
    std::uint64_t m_version = 0;
};

} // namespace Spot
