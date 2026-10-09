#include "physics_debug_view.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

SPOT_COMPONENT(PhysicsDebugView);

namespace {

Spot::Vector3 GetWorldScale(const Spot::Transform& transform) {
    const Spot::Matrix4& matrix = transform.GetWorldMatrix();
    const auto axis = [&matrix](std::size_t i) {
        const Spot::Vector4& column = matrix.columns[i];
        return Spot::Length(Spot::Vector3{column.x, column.y, column.z});
    };
    return {axis(0), axis(1), axis(2)};
}

} // namespace

void PhysicsDebugView::OnUpdate(float /*deltaTime*/) {
    if (Spot::Input::IsKeyPressed(Spot::Key::B)) {
        visible = !visible;
    }
    if (!visible) {
        return;
    }
    GetScene().ForEachComponent<Spot::Collider>([](Spot::Collider& collider) {
        const Spot::Entity& entity = collider.GetEntity();
        const Spot::Transform& transform = entity.GetTransform();
        const Spot::Vector3 scale = GetWorldScale(transform);
        const Spot::Quaternion rotation = transform.GetWorldRotation();
        const Spot::Vector3 center = transform.GetWorldPosition() + rotation * (collider.GetCenter() * scale);
        const bool moving = entity.HasComponent<Spot::Rigidbody>();
        const Spot::DebugDrawOptions options{.color = moving ? Spot::Color{1.0f, 0.5f, 0.0f, 1.0f}
                                                             : Spot::Color{0.2f, 1.0f, 0.3f, 1.0f}};
        if (const auto* box = dynamic_cast<const Spot::BoxCollider*>(&collider)) {
            Spot::DebugDraw::Box(center, box->GetSize() * scale, rotation, options);
        } else if (const auto* sphere = dynamic_cast<const Spot::SphereCollider*>(&collider)) {
            Spot::DebugDraw::Sphere(center, sphere->GetRadius() * std::max({scale.x, scale.y, scale.z}), options);
        } else if (const auto* capsule = dynamic_cast<const Spot::CapsuleCollider*>(&collider)) {
            const float radius = capsule->GetRadius() * std::max(scale.x, scale.z);
            const float halfHeight = std::max(capsule->GetHeight() * scale.y * 0.5f - radius, 0.0f);
            const Spot::Vector3 up = rotation * Spot::Vector3{0.0f, halfHeight, 0.0f};
            Spot::DebugDraw::Capsule(center - up, center + up, radius, options);
        }
    });
}

void PhysicsDebugView::Serialize(Spot::Archive& archive) {
    archive.Field("visible", visible);
}
