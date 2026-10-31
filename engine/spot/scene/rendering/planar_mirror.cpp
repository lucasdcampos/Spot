#include "spot/scene/rendering/planar_mirror.h"
#include "spot/core/io/archive.h"
#include "spot/scene/entity.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Spot {

bool PlanarMirror::IsVisible(const Camera3D& viewer, Vector2 viewSize) const {
    const auto& transform = GetEntity().GetTransform();
    const Vector3 center = transform.GetWorldPosition();
    const Vector3 normal = transform.GetWorldRotation() * Vector3{0, 0, 1};
    if (Dot(viewer.position - center, normal) <= 0.05f || Length(viewer.position - center) > maxDistance) {
        return false;
    }
    const Matrix4 clip = viewer.GetViewProjection(viewSize) * transform.GetWorldMatrix();
    std::array<Vector4, 4> corners;
    for (int i = 0; i < 4; ++i) {
        corners[i] = clip * Vector4{(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, 0, 1};
    }
    for (int plane = 0; plane < 6; ++plane) {
        if (std::all_of(corners.begin(), corners.end(), [plane](Vector4 p) {
                switch (plane) {
                case 0:
                    return p.x < -p.w;
                case 1:
                    return p.x > p.w;
                case 2:
                    return p.y < -p.w;
                case 3:
                    return p.y > p.w;
                case 4:
                    return p.z < 0;
                default:
                    return p.z > p.w;
                }
            })) {
            return false;
        }
    }
    return true;
}

Camera3D PlanarMirror::GetReflectionCamera(const Camera3D& viewer) const {
    const auto& transform = GetEntity().GetTransform();
    const Vector3 center = transform.GetWorldPosition();
    const Quaternion rotation = transform.GetWorldRotation();
    const Vector3 normal = rotation * Vector3{0, 0, 1};
    const float distance = Dot(viewer.position - center, normal);
    Camera3D camera = viewer;
    camera.position = viewer.position - normal * (2 * distance);
    camera.LookAt(camera.position + normal, rotation * Vector3{0, 1, 0});
    camera.cullingMask = reflectionMask;
    camera.orthographic = false;
    camera.pixelPerfect.enabled = false;
    camera.nearPlane = std::max(distance, 0.01f);
    camera.farPlane = std::max(viewer.farPlane, camera.nearPlane + 1);
    const Matrix4 world = transform.GetWorldMatrix();
    const float width = Length(Vector3{world.columns[0].x, world.columns[0].y, world.columns[0].z});
    const float height = Length(Vector3{world.columns[1].x, world.columns[1].y, world.columns[1].z});
    const Vector3 offset = center - camera.position;
    const float x = Dot(offset, camera.GetRight());
    const float y = Dot(offset, camera.GetUp());
    Matrix4 projection = Matrix4::Perspective(viewer.fieldOfView, 1, camera.nearPlane, camera.farPlane);
    projection.columns[0].x = 2 * camera.nearPlane / std::max(width, 0.001f);
    projection.columns[1].y = 2 * camera.nearPlane / std::max(height, 0.001f);
    projection.columns[2].x = 2 * x / std::max(width, 0.001f);
    projection.columns[2].y = 2 * y / std::max(height, 0.001f);
    camera.projectionOverride = projection;
    return camera;
}

void PlanarMirror::Serialize(Archive& archive) {
    archive.Field("maxDistance", maxDistance, {.min = 0.0f, .max = 1000.0f});
    archive.Field("resolution", resolution);
    archive.Field("reflectionMask", reflectionMask);
}

} // namespace Spot
