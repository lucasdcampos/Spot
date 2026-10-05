#pragma once

#include "math/angles.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"

namespace Spot {

// A perspective view. With no rotation it looks down -Z with Y up, like the rest of the engine.
struct Camera3D {
    Vector3 position = {};
    Quaternion rotation = {};
    // Vertical, in radians.
    float fieldOfView = ToRadians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

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
