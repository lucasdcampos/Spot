#include "scene/rendering/camera.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
#include "scene/entity.h"
#include "scene/scene.h"

#include <array>
#include <string_view>

namespace Spot {

Camera3D Camera::GetCamera3D() const {
    const Transform& transform = GetEntity().GetTransform();
    return {.position = transform.GetWorldPosition(),
            .rotation = transform.GetWorldRotation(),
            .fieldOfView = m_fieldOfView,
            .nearPlane = m_nearPlane,
            .farPlane = m_farPlane,
            .orthographic = m_projection == CameraProjection::Orthographic,
            .orthographicSize = m_orthographicSize,
            .cullingMask = m_cullingMask,
            .pixelPerfect = m_pixelPerfect};
}

void Camera::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 2> Projections{"Perspective", "Orthographic"};
    archive.Field("projection", m_projection, {.enumNames = Projections});
    archive.Field("pixelPerfect", m_pixelPerfect,
                  {.tooltip = "Integer scaling and nearest sampling for orthographic pixel art"});
    archive.Field("fieldOfView", m_fieldOfView,
                  {.min = ToRadians(1.0f), .max = ToRadians(179.0f), .angle = true, .tooltip = "Vertical, in degrees"});
    archive.Field("orthographicSize", m_orthographicSize,
                  {.min = 0.001f, .max = 100000.0f, .speed = 0.05f, .tooltip = "Half the view's height, in units"});
    archive.Field("nearPlane", m_nearPlane, {.min = 0.001f, .max = 100000.0f, .speed = 0.01f});
    archive.Field("farPlane", m_farPlane, {.min = 0.01f, .max = 1000000.0f, .speed = 1.0f});
    archive.Field("background", m_background, {.tooltip = "What shows where nothing is drawn"});
    archive.Field("cullingMask", m_cullingMask, {.tooltip = "The layers whose meshes it sees", .layerMask = true});
}

Camera* FindMainCamera(Scene& scene) {
    Camera* found = nullptr;
    scene.ForEachComponent<Camera>([&](Camera& camera) {
        if (!found) {
            found = &camera;
        }
    });
    return found;
}

} // namespace Spot
