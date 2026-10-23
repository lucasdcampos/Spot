#pragma once

#include "spot/graphics/camera_3d.h"
#include "spot/math/angles.h"
#include "spot/math/color.h"
#include "spot/scene/component.h"

namespace Spot {

class Scene;

enum class CameraProjection {
    // Farther things look smaller, as they do to the eye.
    Perspective,
    // Things keep their size however far they are, as 2D games and technical views show them.
    Orthographic,
};

// Where a scene is seen from when it runs: its entity's position, looking down the entity's -Z. The scene's main
// camera is the first one enabled on an active entity (see FindMainCamera()), which games render through:
//
//     if (const Spot::Camera* camera = Spot::FindMainCamera(scene)) {
//         renderer.Begin(context, {.depthTarget = depth.get(), .clearDepth = 1.0f}, camera->GetCamera3D());
//     }
class Camera final : public Component {
public:
    CameraProjection GetProjection() const { return m_projection; }
    void SetProjection(CameraProjection projection) { m_projection = projection; }

    // Perspective cameras' vertical field of view, in radians.
    float GetFieldOfView() const { return m_fieldOfView; }
    void SetFieldOfView(float fieldOfView) { m_fieldOfView = fieldOfView; }

    // Orthographic cameras' half height, in world units.
    float GetOrthographicSize() const { return m_orthographicSize; }
    void SetOrthographicSize(float size) { m_orthographicSize = size; }

    // How near and far from the camera things are seen.
    float GetNearPlane() const { return m_nearPlane; }
    void SetNearPlane(float distance) { m_nearPlane = distance; }
    float GetFarPlane() const { return m_farPlane; }
    void SetFarPlane(float distance) { m_farPlane = distance; }

    // What shows where nothing is drawn, such as in scenes without a sky.
    Color GetBackground() const { return m_background; }
    void SetBackground(Color background) { m_background = background; }
    // The layers whose meshes it sees; see Camera3D::cullingMask.
    std::uint32_t GetCullingMask() const { return m_cullingMask; }
    void SetCullingMask(std::uint32_t mask) { m_cullingMask = mask; }
    bool SeesLayer(int layer) const { return (m_cullingMask >> layer) & 1u; }
    void SetLayerSeen(int layer, bool seen) {
        m_cullingMask = seen ? m_cullingMask | (1u << layer) : m_cullingMask & ~(1u << layer);
    }

    // The camera's view from its entity's world position and rotation, as Renderer3D takes it; Renderer2D takes its
    // GetViewProjection().
    Camera3D GetCamera3D() const;
    bool IsPixelPerfect() const { return m_pixelPerfect.enabled; }
    void SetPixelPerfect(bool enabled) { m_pixelPerfect.enabled = enabled; }
    float GetPixelsPerUnit() const { return m_pixelPerfect.pixelsPerUnit; }
    void SetPixelsPerUnit(float pixels) { m_pixelPerfect.pixelsPerUnit = pixels; }
    Vector2 GetReferenceResolution() const { return m_pixelPerfect.referenceResolution; }
    void SetReferenceResolution(Vector2 size) { m_pixelPerfect.referenceResolution = size; }

protected:
    void Serialize(Archive& archive) override;

private:
    CameraProjection m_projection = CameraProjection::Perspective;
    float m_fieldOfView = ToRadians(60.0f);
    float m_orthographicSize = 5.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;
    Color m_background{0.12f, 0.13f, 0.15f, 1.0f};
    std::uint32_t m_cullingMask = 0xFFFFFFFFu;
    PixelPerfectSettings m_pixelPerfect;
};

// The first Camera, in creation order, that is enabled on an entity active in the hierarchy; null when the scene
// has none.
Camera* FindMainCamera(Scene& scene);

} // namespace Spot
