#pragma once

#include "spot/math/quaternion.h"
#include "spot/math/vector.h"

#include <array>

namespace Spot {

// Column-major 4x4 matrix for column vectors (M * v), as GPUs expect it. Defaults to the identity.
// Coordinates are right-handed with Y up and the camera looking down -Z. Projections map depth to [0, 1], the
// clip space of Vulkan, Metal and Direct3D.
struct Matrix4 {
    std::array<Vector4, 4> columns = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};

    static constexpr Matrix4 Identity() { return {}; }
    static Matrix4 Translation(Vector3 offset);
    static Matrix4 Scale(Vector3 scale);
    static Matrix4 Rotation(const Quaternion& rotation);
    static Matrix4 Perspective(float verticalFov, float aspectRatio, float nearPlane, float farPlane);
    static Matrix4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
    static Matrix4 LookAt(Vector3 eye, Vector3 target, Vector3 up);

    // The 16 floats in column-major order, ready to upload to the GPU.
    const float* GetData() const { return &columns[0].x; }

    friend constexpr bool operator==(const Matrix4&, const Matrix4&) = default;
};

// Applies b first, then a.
Matrix4 operator*(const Matrix4& a, const Matrix4& b);
Vector4 operator*(const Matrix4& m, Vector4 v);

Matrix4 Inverse(const Matrix4& m);
Matrix4 Transpose(const Matrix4& m);

// A translation, a rotation and a scale, which Translation(translation) * Rotation(rotation) * Scale(scale) combines.
struct TransformComponents {
    Vector3 translation = {};
    Quaternion rotation = {};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

// Splits a matrix back into the translation, rotation and scale it combines, as tools do to edit a world transform as
// a local one. A mirrored matrix gives a negative X scale. Shear, such as a non-uniform scale under a rotated parent
// makes, cannot be represented and is dropped.
TransformComponents Decompose(const Matrix4& m);

} // namespace Spot
