#include "scene/transform.h"

#include "scene/entity.h"

namespace Spot {

void Transform::SetPosition(Vector3 position) {
    m_hasWorldOverride = false;
    m_position = position;
    MarkWorldDirty();
}

void Transform::SetRotation(const Quaternion& rotation) {
    m_hasWorldOverride = false;
    m_rotation = rotation;
    MarkWorldDirty();
}

void Transform::SetRotation2D(float angle) {
    SetRotation(Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, angle));
}

float Transform::GetRotation2D() const {
    return ToEuler(m_rotation).z;
}

void Transform::SetScale(Vector3 scale) {
    m_hasWorldOverride = false;
    m_scale = scale;
    MarkWorldDirty();
}

Matrix4 Transform::GetLocalMatrix() const {
    return Matrix4::Translation(m_position) * Matrix4::Rotation(m_rotation) * Matrix4::Scale(m_scale);
}

void Transform::SetWorldOverride(const Matrix4& matrix) {
    m_worldOverride = matrix;
    m_hasWorldOverride = true;
    MarkWorldDirty();
}
void Transform::ClearWorldOverride() {
    if (m_hasWorldOverride) {
        m_hasWorldOverride = false;
        MarkWorldDirty();
    }
}

const Matrix4& Transform::GetWorldMatrix() const {
    UpdateWorld();
    return m_worldMatrix;
}

Vector3 Transform::GetWorldPosition() const {
    const Vector4& origin = GetWorldMatrix().columns[3];
    return {origin.x, origin.y, origin.z};
}

void Transform::SetWorldPosition(Vector3 position) {
    const Entity* parent = m_entity.GetParent();
    if (!parent) {
        SetPosition(position);
        return;
    }
    const Vector4 local =
        Inverse(parent->GetTransform().GetWorldMatrix()) * Vector4{position.x, position.y, position.z, 1.0f};
    SetPosition({local.x, local.y, local.z});
}

const Quaternion& Transform::GetWorldRotation() const {
    UpdateWorld();
    return m_worldRotation;
}

void Transform::SetWorldRotation(const Quaternion& rotation) {
    const Entity* parent = m_entity.GetParent();
    SetRotation(parent ? Inverse(parent->GetTransform().GetWorldRotation()) * rotation : rotation);
}

float Transform::GetWorldRotation2D() const {
    return ToEuler(GetWorldRotation()).z;
}

void Transform::MarkWorldDirty() {
    // Already outdated world values have changed since anyone read them, so their version already tells.
    if (m_worldDirty) {
        return;
    }
    m_worldDirty = true;
    ++m_version;
    for (Entity* child : m_entity.m_children) {
        child->m_transform.MarkWorldDirty();
    }
}

void Transform::UpdateWorld() const {
    if (!m_worldDirty) {
        return;
    }
    if (m_hasWorldOverride) {
        m_worldMatrix = m_worldOverride;
        m_worldRotation = Decompose(m_worldOverride).rotation;
    } else if (const Entity* parent = m_entity.GetParent()) {
        const Transform& parentTransform = parent->GetTransform();
        m_worldMatrix = parentTransform.GetWorldMatrix() * GetLocalMatrix();
        m_worldRotation = parentTransform.GetWorldRotation() * m_rotation;
    } else {
        m_worldMatrix = GetLocalMatrix();
        m_worldRotation = m_rotation;
    }
    m_worldDirty = false;
}

} // namespace Spot
