#include "spot/scene/physics/physics_debug.h"

#include "spot/graphics/debug_draw.h"
#include "spot/scene/physics/character_controller.h"
#include "spot/scene/physics/collider.h"
#include "spot/scene/physics/joint.h"
#include "spot/scene/physics/physics_world.h"
#include "spot/scene/physics/rigidbody.h"
#include "spot/scene/scene.h"

#include <algorithm>
#include <cstddef>

namespace Spot {

namespace {

constexpr Color Static{0.2f, 1.0f, 0.3f, 1.0f};
constexpr Color Dynamic{1.0f, 0.55f, 0.1f, 1.0f};
constexpr Color Sleeping{0.35f, 0.5f, 0.9f, 1.0f};
constexpr Color Kinematic{0.75f, 0.35f, 1.0f, 1.0f};
constexpr Color Trigger{1.0f, 0.9f, 0.1f, 1.0f};
constexpr Color Character{0.2f, 0.9f, 1.0f, 1.0f};
constexpr Color JointColor{1.0f, 1.0f, 1.0f, 1.0f};
// Meshes with more triangles than this show as their bounds, which says where they are at a fraction of the lines.
constexpr std::size_t MaxWireTriangles = 20000;

Vector3 ToWorld(const Transform& transform, Vector3 point) {
    const Vector4 world = transform.GetWorldMatrix() * Vector4{point.x, point.y, point.z, 1.0f};
    return {world.x, world.y, world.z};
}

Vector3 GetWorldScale(const Transform& transform) {
    const Matrix4& matrix = transform.GetWorldMatrix();
    const auto axis = [&matrix](std::size_t i) {
        const Vector4& column = matrix.columns[i];
        return Length(Vector3{column.x, column.y, column.z});
    };
    return {axis(0), axis(1), axis(2)};
}

Color GetBodyColor(const Entity& entity) {
    const Rigidbody* rigidbody = entity.GetComponent<Rigidbody>();
    if (!rigidbody || !rigidbody->IsActiveAndEnabled()) {
        return Static;
    }
    if (rigidbody->IsKinematic()) {
        return Kinematic;
    }
    // Only scenes whose physics runs have bodies that sleep; asking would make the others' physics.
    const bool sleeping = entity.GetScene().GetSystem<PhysicsWorld>() && rigidbody->IsSleeping();
    return sleeping ? Sleeping : Dynamic;
}

void DrawMesh(const MeshData& mesh, const Transform& transform, Vector3 center, DebugLines& lines,
              const DebugDrawOptions& options) {
    if (mesh.indices.size() / 3 > MaxWireTriangles) {
        const BoundingBox box = mesh.GetBoundingBox();
        lines.Box(ToWorld(transform, box.GetCenter() + center), box.GetSize() * GetWorldScale(transform),
                  transform.GetWorldRotation(), options);
        return;
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vector3 a = ToWorld(transform, mesh.vertices[mesh.indices[i]].position + center);
        const Vector3 b = ToWorld(transform, mesh.vertices[mesh.indices[i + 1]].position + center);
        const Vector3 c = ToWorld(transform, mesh.vertices[mesh.indices[i + 2]].position + center);
        lines.Line(a, b, options);
        lines.Line(b, c, options);
        lines.Line(c, a, options);
    }
}

void DrawCollider(const Collider& collider, DebugLines& lines) {
    const Transform& transform = collider.GetEntity().GetTransform();
    const Vector3 scale = GetWorldScale(transform);
    const Quaternion rotation = transform.GetWorldRotation();
    const Vector3 center = ToWorld(transform, collider.GetCenter());
    const DebugDrawOptions options{.color = collider.IsTrigger() ? Trigger : GetBodyColor(collider.GetEntity())};
    if (const auto* box = dynamic_cast<const BoxCollider*>(&collider)) {
        lines.Box(center, box->GetSize() * scale, rotation, options);
    } else if (const auto* sphere = dynamic_cast<const SphereCollider*>(&collider)) {
        lines.Sphere(center, sphere->GetRadius() * std::max({scale.x, scale.y, scale.z}), options);
    } else if (const auto* capsule = dynamic_cast<const CapsuleCollider*>(&collider)) {
        const float radius = capsule->GetRadius() * std::max(scale.x, scale.z);
        const float halfHeight = std::max(capsule->GetHeight() * scale.y * 0.5f - radius, 0.0f);
        const Vector3 up = rotation * Vector3{0.0f, halfHeight, 0.0f};
        lines.Capsule(center - up, center + up, radius, options);
    } else if (const auto* mesh = dynamic_cast<const MeshCollider*>(&collider)) {
        if (const MeshData* data = mesh->GetMesh().Get()) {
            DrawMesh(*data, transform, collider.GetCenter(), lines, options);
        }
    }
}

void DrawCharacter(const CharacterController& controller, DebugLines& lines) {
    const Vector3 center = controller.GetEntity().GetTransform().GetWorldPosition() + controller.GetCenter();
    const float radius = controller.GetRadius();
    const float halfHeight = std::max(controller.GetHeight() * 0.5f - radius, 0.0f);
    lines.Capsule(center - Vector3{0.0f, halfHeight, 0.0f}, center + Vector3{0.0f, halfHeight, 0.0f}, radius,
                  {.color = Character});
}

void DrawJoint(Scene& scene, const Joint& joint, DebugLines& lines) {
    const Transform& transform = joint.GetEntity().GetTransform();
    const Vector3 anchor = ToWorld(transform, joint.GetAnchor());
    const DebugDrawOptions options{.color = JointColor, .depthTest = false};
    lines.Cross(anchor, 0.15f, options);
    lines.Line(transform.GetWorldPosition(), anchor, options);
    if (const Entity* connected = scene.GetEntity(joint.GetConnectedEntity())) {
        lines.Line(anchor, connected->GetTransform().GetWorldPosition(), options);
    }
}

} // namespace

void DrawColliders(Scene& scene, DebugLines& lines) {
    scene.ForEachComponent<Collider>([&lines](const Collider& collider) { DrawCollider(collider, lines); });
    scene.ForEachComponent<CharacterController>(
        [&lines](const CharacterController& controller) { DrawCharacter(controller, lines); });
    scene.ForEachComponent<Joint>([&scene, &lines](const Joint& joint) { DrawJoint(scene, joint, lines); });
}

} // namespace Spot
