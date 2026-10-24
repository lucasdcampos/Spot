#include "spot/scene/scripting/script_module_bindings.h"

#include "spot/scene/rendering/camera.h"
#include "spot/scene/rendering/sprite_renderer.h"

namespace Spot::Detail {

void RegisterRenderingScriptBindings() {

    ScriptBindings<SpriteRenderer>(BuiltinScriptBindingsTag{})
        .Property("color", &SpriteRenderer::GetColor, &SpriteRenderer::SetColor)
        .Property("size", &SpriteRenderer::GetSize,
                  [](SpriteRenderer& sprite, Vector2 value) {
                      if (value.x < 0 || value.y < 0)
                          throw std::runtime_error("Sprite size cannot be negative");
                      sprite.SetSize(value);
                  })
        .Property("frame", &SpriteRenderer::GetFrame, &SpriteRenderer::SetFrame)
        .Property("flippedX", &SpriteRenderer::IsFlippedX, &SpriteRenderer::SetFlippedX)
        .Property("flippedY", &SpriteRenderer::IsFlippedY, &SpriteRenderer::SetFlippedY)
        .Property("order", &SpriteRenderer::GetOrder, &SpriteRenderer::SetOrder);
    ScriptBindings<Camera>(BuiltinScriptBindingsTag{})
        .Property(
            "projection",
            [](const Camera& camera) {
                return std::string(camera.GetProjection() == CameraProjection::Perspective ? "Perspective"
                                                                                           : "Orthographic");
            },
            [](Camera& camera, const std::string& value) {
                if (value != "Perspective" && value != "Orthographic")
                    throw std::runtime_error("Unknown camera projection");
                camera.SetProjection(value == "Perspective" ? CameraProjection::Perspective
                                                            : CameraProjection::Orthographic);
            })
        .Property("fieldOfView", &Camera::GetFieldOfView,
                  [](Camera& camera, float value) {
                      if (value <= 0 || value >= ToRadians(180.0f))
                          throw std::runtime_error("Field of view must be between 0 and pi radians");
                      camera.SetFieldOfView(value);
                  })
        .Property("orthographicSize", &Camera::GetOrthographicSize,
                  [](Camera& camera, float value) {
                      if (value <= 0)
                          throw std::runtime_error("Orthographic size must be positive");
                      camera.SetOrthographicSize(value);
                  })
        .Property("nearPlane", &Camera::GetNearPlane,
                  [](Camera& camera, float value) {
                      if (value <= 0 || value >= camera.GetFarPlane())
                          throw std::runtime_error("Near plane must be positive and below far plane");
                      camera.SetNearPlane(value);
                  })
        .Property("farPlane", &Camera::GetFarPlane,
                  [](Camera& camera, float value) {
                      if (value <= camera.GetNearPlane())
                          throw std::runtime_error("Far plane must exceed near plane");
                      camera.SetFarPlane(value);
                  })
        .Property("background", &Camera::GetBackground, &Camera::SetBackground);
    PropertyMethods<SpriteRenderer>("color", "GetColor", "SetColor");
    PropertyMethods<SpriteRenderer>("size", "GetSize", "SetSize");
    PropertyMethods<SpriteRenderer>("frame", "GetFrame", "SetFrame");
    PropertyMethods<SpriteRenderer>("flippedX", "IsFlippedX", "SetFlippedX");
    PropertyMethods<SpriteRenderer>("flippedY", "IsFlippedY", "SetFlippedY");
    PropertyMethods<SpriteRenderer>("order", "GetOrder", "SetOrder");
    PropertyMethods<Camera>("projection", "GetProjection", "SetProjection");
    PropertyMethods<Camera>("fieldOfView", "GetFieldOfView", "SetFieldOfView");
    PropertyMethods<Camera>("orthographicSize", "GetOrthographicSize", "SetOrthographicSize");
    PropertyMethods<Camera>("nearPlane", "GetNearPlane", "SetNearPlane");
    PropertyMethods<Camera>("farPlane", "GetFarPlane", "SetFarPlane");
    PropertyMethods<Camera>("background", "GetBackground", "SetBackground");
}

} // namespace Spot::Detail
