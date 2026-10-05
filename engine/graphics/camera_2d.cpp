#include "graphics/camera_2d.h"

#include <cmath>

namespace Spot {

Matrix4 Camera2D::GetViewProjection(Vector2 viewSize) const {
    const Vector2 half = viewSize / 2.0f;
    const auto projection = Matrix4::Orthographic(-half.x, half.x, -half.y, half.y, -1.0f, 1.0f);
    const auto view = Matrix4::Scale({zoom, zoom, 1.0f}) *
                      Matrix4::Rotation(Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, -rotation)) *
                      Matrix4::Translation({-position.x, -position.y, 0.0f});
    return projection * view;
}

Vector2 Camera2D::ScreenToWorld(Vector2 screenPosition, Vector2 viewSize) const {
    const Vector2 fromCenter{screenPosition.x - viewSize.x / 2.0f, viewSize.y / 2.0f - screenPosition.y};
    const Vector2 scaled = fromCenter / zoom;
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    return position + Vector2{scaled.x * cosine - scaled.y * sine, scaled.x * sine + scaled.y * cosine};
}

} // namespace Spot
