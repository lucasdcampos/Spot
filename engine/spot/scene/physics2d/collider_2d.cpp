#include "spot/scene/physics2d/collider_2d.h"
#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"
#include "spot/scene/physics2d/physics_world_2d.h"
#include <algorithm>
#include <cmath>

namespace Spot {
void Collider2D::OnEnable() {
    GetScene().GetPhysics2D();
}
Shape2D Collider2D::GetShape(Vector2 scale) const {
    auto result = m_shape;
    result.center = result.center * scale;
    for (auto& vertex : result.vertices)
        vertex = vertex * scale;
    scale = {std::abs(scale.x), std::abs(scale.y)};
    result.size = result.size * scale;
    result.radius *= std::max(scale.x, scale.y);
    result.height = std::max(result.height * scale.y, 2.0f * result.radius);
    return result;
}
void Collider2D::AppendShapes(Vector2 scale, std::vector<Shape2D>& shapes) const {
    shapes.push_back(GetShape(scale));
}
void Collider2D::Serialize(Archive& archive) {
    archive.Field("center", m_shape.center);
    archive.Field("friction", m_shape.friction, {.min = 0.0f, .max = 10.0f});
    archive.Field("restitution", m_shape.restitution, {.min = 0.0f, .max = 1.0f});
    archive.Field("trigger", m_shape.trigger);
}
void BoxCollider2D::Serialize(Archive& archive) {
    Collider2D::Serialize(archive);
    archive.Field("size", m_shape.size);
}
void CircleCollider2D::Serialize(Archive& archive) {
    Collider2D::Serialize(archive);
    archive.Field("radius", m_shape.radius);
}
void CapsuleCollider2D::Serialize(Archive& archive) {
    Collider2D::Serialize(archive);
    archive.Field("radius", m_shape.radius);
    archive.Field("height", m_shape.height);
}
void PolygonCollider2D::Serialize(Archive& archive) {
    Collider2D::Serialize(archive);
    archive.Field("vertices", m_shape.vertices);
}
} // namespace Spot
