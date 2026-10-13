#include "graphics/camera_3d.h"

namespace Spot {

void Camera3D::LookAt(Vector3 target, Vector3 up) {
    if (target != position) {
        rotation = Quaternion::LookRotation(target - position, up);
    }
}

Vector3 Camera3D::GetForward() const {
    return rotation * Vector3{0.0f, 0.0f, -1.0f};
}

Vector3 Camera3D::GetRight() const {
    return rotation * Vector3{1.0f, 0.0f, 0.0f};
}

Vector3 Camera3D::GetUp() const {
    return rotation * Vector3{0.0f, 1.0f, 0.0f};
}

Matrix4 Camera3D::GetView() const {
    return Matrix4::Rotation(Inverse(rotation)) * Matrix4::Translation(-position);
}

Matrix4 Camera3D::GetProjection(Vector2 viewSize) const {
    const float aspectRatio = viewSize.y > 0.0f ? viewSize.x / viewSize.y : 1.0f;
    if (orthographic) {
        if (pixelPerfect.enabled && pixelPerfect.IsValid()) {
            const auto half = viewSize / (2 * pixelPerfect.GetZoom(viewSize));
            return Matrix4::Orthographic(-half.x, half.x, -half.y, half.y, nearPlane, farPlane);
        }
        const float halfWidth = orthographicSize * aspectRatio;
        return Matrix4::Orthographic(-halfWidth, halfWidth, -orthographicSize, orthographicSize, nearPlane, farPlane);
    }
    return Matrix4::Perspective(fieldOfView, aspectRatio, nearPlane, farPlane);
}

Matrix4 Camera3D::GetViewProjection(Vector2 viewSize) const {
    if (orthographic && pixelPerfect.enabled && pixelPerfect.IsValid()) {
        auto resolved = *this;
        const auto local = Inverse(rotation) * position;
        const auto snapped = pixelPerfect.SnapCamera({local.x, local.y}, viewSize);
        resolved.position = rotation * Vector3{snapped.x, snapped.y, local.z};
        return GetProjection(viewSize) * resolved.GetView();
    }
    return GetProjection(viewSize) * GetView();
}

} // namespace Spot
