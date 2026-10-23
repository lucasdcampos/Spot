#pragma once

#include "spot/graphics/pixel_perfect.h"
#include "spot/math/angles.h"
#include "spot/math/matrix.h"
#include "spot/math/quaternion.h"
#include "spot/math/vector.h"

#include <cstdint>

namespace Spot {

// A perspective view, or an orthographic one. With no rotation it looks down -Z with Y up, like the rest of the
// engine.
struct Camera3D {
    Vector3 position = {};
    Quaternion rotation = {};
    // Vertical, in radians.
    float fieldOfView = ToRadians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    // Sees along parallel lines, so things keep their size however far they are, as 2D games and technical views
    // show them; the field of view is then unused.
    bool orthographic = false;
    // Half the height of what an orthographic camera sees, in world units.
    float orthographicSize = 5.0f;
    // The layers whose meshes Renderer3D::DrawScene() draws for this camera, a bit per layer (see Entity::GetLayer()),
    // as first-person games hide the player's own body from its eyes.
    std::uint32_t cullingMask = 0xFFFFFFFFu;
    PixelPerfectSettings pixelPerfect = {};

    // Turns the camera toward the target, keeping the up direction as close to up as it allows.
    void LookAt(Vector3 target, Vector3 up = {0.0f, 1.0f, 0.0f});

    Vector3 GetForward() const;
    Vector3 GetRight() const;
    Vector3 GetUp() const;

    Matrix4 GetView() const;
    // The view size only sets the aspect ratio, so it can be in pixels or points.
    Matrix4 GetProjection(Vector2 viewSize) const;
    Matrix4 GetViewProjection(Vector2 viewSize) const;
};

} // namespace Spot
