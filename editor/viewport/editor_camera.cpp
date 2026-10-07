#include "viewport/editor_camera.h"

#include <algorithm>
#include <cmath>

namespace Spot::Editor {

namespace {

constexpr float TurnPerPixel = 0.004f;
constexpr float FastFactor = 3.0f;
constexpr float ZoomPerNotch = 0.85f;
constexpr float SpeedPerNotch = 1.2f;
constexpr float MinDistance = 0.05f;
constexpr float MinSpeed = 0.05f;
constexpr float MaxSpeed = 1000.0f;
// Short of straight up or down, where turning around the vertical would spin the view.
const float MaxPitch = ToRadians(89.0f);

} // namespace

EditorCamera::EditorCamera() : m_pivot{}, m_yaw(ToRadians(45.0f)), m_pitch(ToRadians(-30.0f)), m_distance(10.0f) {
}

Quaternion EditorCamera::GetRotation() const {
    // Pitch first, around the camera's own X, then yaw around the world's vertical.
    return Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, m_yaw) *
           Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, m_pitch);
}

Vector3 EditorCamera::GetPosition() const {
    return m_pivot - GetRotation() * Vector3{0.0f, 0.0f, -1.0f} * m_distance;
}

Camera3D EditorCamera::GetCamera() const {
    Camera3D camera;
    camera.position = GetPosition();
    camera.rotation = GetRotation();
    camera.fieldOfView = m_fieldOfView;
    // Close views of small things need a near plane closer than the default.
    camera.nearPlane = std::clamp(m_distance * 0.01f, 0.01f, 0.1f);
    camera.farPlane = 5000.0f;
    return camera;
}

void EditorCamera::SetSpeed(float speed) {
    m_speed = std::clamp(speed, MinSpeed, MaxSpeed);
}

void EditorCamera::SetView(Vector3 pivot, float yaw, float pitch, float distance) {
    m_pivot = pivot;
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -MaxPitch, MaxPitch);
    m_distance = std::max(distance, MinDistance);
}

void EditorCamera::Update(const EditorCameraInput& input) {
    if (input.look) {
        // Turning while flying turns the eye in place, so the pivot moves around it.
        const Vector3 eye = GetPosition();
        m_yaw -= input.mouseDelta.x * TurnPerPixel;
        m_pitch = std::clamp(m_pitch - input.mouseDelta.y * TurnPerPixel, -MaxPitch, MaxPitch);
        const Quaternion rotation = GetRotation();
        const Vector3 forward = rotation * Vector3{0.0f, 0.0f, -1.0f};
        const Vector3 right = rotation * Vector3{1.0f, 0.0f, 0.0f};
        const Vector3 up = {0.0f, 1.0f, 0.0f};
        const float step = m_speed * (input.fast ? FastFactor : 1.0f) * input.deltaTime;
        const Vector3 moved = eye + (right * input.move.x + up * input.move.y + forward * input.move.z) * step;
        m_pivot = moved + forward * m_distance;
        if (input.wheel != 0.0f) {
            SetSpeed(m_speed * std::pow(SpeedPerNotch, input.wheel));
        }
        return;
    }

    if (input.orbit) {
        m_yaw -= input.mouseDelta.x * TurnPerPixel;
        m_pitch = std::clamp(m_pitch - input.mouseDelta.y * TurnPerPixel, -MaxPitch, MaxPitch);
    }

    if (input.pan) {
        // Whatever is at the pivot's depth stays under the mouse.
        const float unitsPerPixel =
            2.0f * m_distance * std::tan(m_fieldOfView * 0.5f) / std::max(input.viewHeight, 1.0f);
        const Quaternion rotation = GetRotation();
        const Vector3 right = rotation * Vector3{1.0f, 0.0f, 0.0f};
        const Vector3 up = rotation * Vector3{0.0f, 1.0f, 0.0f};
        m_pivot = m_pivot - right * (input.mouseDelta.x * unitsPerPixel) + up * (input.mouseDelta.y * unitsPerPixel);
    }

    if (input.wheel != 0.0f) {
        const float distance = m_distance * std::pow(ZoomPerNotch, input.wheel);
        if (distance < MinDistance) {
            // Zooming past the pivot pushes it ahead, so the view keeps moving in.
            const Vector3 forward = GetRotation() * Vector3{0.0f, 0.0f, -1.0f};
            m_pivot = m_pivot + forward * (m_distance - MinDistance);
            m_distance = MinDistance;
        } else {
            m_distance = distance;
        }
    }
}

void EditorCamera::Frame(Vector3 center, float radius) {
    m_pivot = center;
    // Far enough that the sphere fits the vertical field of view, with some room around it.
    const float fitted = std::max(radius, 0.1f) / std::sin(m_fieldOfView * 0.5f);
    m_distance = std::max(fitted * 1.1f, MinDistance);
}

} // namespace Spot::Editor
