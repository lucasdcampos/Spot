#include "spot/scene/physics/collider.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"
#include "spot/scene/physics/physics_world.h"
#include "spot/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Spot {

void Collider::SetCenter(Vector3 center) {
    m_center = center;
    MarkChanged();
}

void Collider::SetFriction(float friction) {
    m_friction = friction;
    MarkChanged();
}

void Collider::SetRestitution(float restitution) {
    m_restitution = restitution;
    MarkChanged();
}

void Collider::SetTrigger(bool trigger) {
    m_trigger = trigger;
    MarkChanged();
}

bool Collider::FitTo(const BoundingBox& /*box*/) {
    return false;
}

void Collider::OnEnable() {
    MarkChanged();
}

void Collider::OnDisable() {
    MarkChanged();
}

void Collider::Serialize(Archive& archive) {
    archive.Field("center", m_center, {.speed = 0.01f, .tooltip = "Where the shape is, from the entity"});
    archive.Field("friction", m_friction, {.min = 0.0f, .max = 1.0f, .speed = 0.005f});
    archive.Field("restitution", m_restitution,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .tooltip = "How much it bounces"});
    archive.Field("trigger", m_trigger, {.tooltip = "Detects what overlaps it, without pushing it away"});
}

void Collider::MarkChanged() {
    GetScene().GetOrAddSystem<Spot::PhysicsWorld>().MarkChanged(GetEntity().GetId());
}

void BoxCollider::SetSize(Vector3 size) {
    m_size = size;
    MarkChanged();
}

bool BoxCollider::FitTo(const BoundingBox& box) {
    m_size = box.GetSize();
    SetCenter(box.GetCenter());
    return true;
}

void BoxCollider::Serialize(Archive& archive) {
    Collider::Serialize(archive);
    archive.Field("size", m_size, {.min = 0.0f, .max = 10000.0f, .speed = 0.01f});
}

Shape3D BoxCollider::GetShape(Vector3 scale) const {
    const Vector3 size = m_size * scale;
    return {
        .kind = ShapeKind3D::Box,
        .center = GetCenter() * scale,
        .halfExtents = Vector3{std::abs(size.x), std::abs(size.y), std::abs(size.z)} * 0.5f,
    };
}

void SphereCollider::SetRadius(float radius) {
    m_radius = radius;
    MarkChanged();
}

bool SphereCollider::FitTo(const BoundingBox& box) {
    m_radius = Length(box.GetSize()) * 0.5f;
    SetCenter(box.GetCenter());
    return true;
}

void SphereCollider::Serialize(Archive& archive) {
    Collider::Serialize(archive);
    archive.Field("radius", m_radius, {.min = 0.0f, .max = 10000.0f, .speed = 0.01f});
}

Shape3D SphereCollider::GetShape(Vector3 scale) const {
    const float largestScale = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    return {
        .kind = ShapeKind3D::Sphere,
        .center = GetCenter() * scale,
        .radius = m_radius * largestScale,
    };
}

void CapsuleCollider::SetRadius(float radius) {
    m_radius = radius;
    MarkChanged();
}

void CapsuleCollider::SetHeight(float height) {
    m_height = height;
    MarkChanged();
}

bool CapsuleCollider::FitTo(const BoundingBox& box) {
    const Vector3 size = box.GetSize();
    m_radius = std::max(size.x, size.z) * 0.5f;
    m_height = std::max(size.y, m_radius * 2.0f);
    SetCenter(box.GetCenter());
    return true;
}

void CapsuleCollider::Serialize(Archive& archive) {
    Collider::Serialize(archive);
    archive.Field("radius", m_radius, {.min = 0.0f, .max = 10000.0f, .speed = 0.01f});
    archive.Field("height", m_height,
                  {.min = 0.0f, .max = 10000.0f, .speed = 0.01f, .tooltip = "From end to end, rounded ends included"});
}

Shape3D CapsuleCollider::GetShape(Vector3 scale) const {
    const float radius = m_radius * std::max(std::abs(scale.x), std::abs(scale.z));
    return {
        .kind = ShapeKind3D::Capsule,
        .center = GetCenter() * scale,
        .radius = radius,
        .halfHeight = std::max(m_height * std::abs(scale.y) * 0.5f - radius, 0.0f),
    };
}

void MeshCollider::SetMesh(AssetRef<MeshData> mesh) {
    m_mesh = std::move(mesh);
    MarkChanged();
}

void MeshCollider::SetConvex(bool convex) {
    m_convex = convex;
    MarkChanged();
}

void MeshCollider::Serialize(Archive& archive) {
    Collider::Serialize(archive);
    archive.Field("mesh", m_mesh, {.tooltip = "The mesh asset whose shape it takes"});
    archive.Field(
        "convex", m_convex,
        {.tooltip = "The convex hull around the mesh, which bodies that move need, rather than its triangles"});
}

Shape3D MeshCollider::GetShape(Vector3 scale) const {
    return {
        .kind = m_convex ? ShapeKind3D::ConvexHull : ShapeKind3D::Mesh,
        .center = GetCenter() * scale,
        .mesh = m_mesh.GetShared(),
        .meshScale = scale,
    };
}

} // namespace Spot
