#include "spot/core/math/matrix.h"

#include "spot/core/math/glm_conversions.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>

namespace Spot {

using Detail::FromGlm;
using Detail::ToGlm;

Matrix4 Matrix4::Translation(Vector3 offset) {
    return FromGlm(glm::translate(glm::mat4(1.0f), ToGlm(offset)));
}

Matrix4 Matrix4::Scale(Vector3 scale) {
    return FromGlm(glm::scale(glm::mat4(1.0f), ToGlm(scale)));
}

Matrix4 Matrix4::Rotation(const Quaternion& rotation) {
    return FromGlm(glm::mat4_cast(ToGlm(rotation)));
}

Matrix4 Matrix4::Perspective(float verticalFov, float aspectRatio, float nearPlane, float farPlane) {
    return FromGlm(glm::perspectiveRH_ZO(verticalFov, aspectRatio, nearPlane, farPlane));
}

Matrix4 Matrix4::Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
    return FromGlm(glm::orthoRH_ZO(left, right, bottom, top, nearPlane, farPlane));
}

Matrix4 Matrix4::LookAt(Vector3 eye, Vector3 target, Vector3 up) {
    return FromGlm(glm::lookAtRH(ToGlm(eye), ToGlm(target), ToGlm(up)));
}

Matrix4 operator*(const Matrix4& a, const Matrix4& b) {
    return FromGlm(ToGlm(a) * ToGlm(b));
}

Vector4 operator*(const Matrix4& m, Vector4 v) {
    return FromGlm(ToGlm(m) * ToGlm(v));
}

Matrix4 Inverse(const Matrix4& m) {
    return FromGlm(glm::inverse(ToGlm(m)));
}

Matrix4 Transpose(const Matrix4& m) {
    return FromGlm(glm::transpose(ToGlm(m)));
}

TransformComponents Decompose(const Matrix4& m) {
    const glm::mat4 matrix = ToGlm(m);
    std::array<glm::vec3, 3> axes{glm::vec3(matrix[0]), glm::vec3(matrix[1]), glm::vec3(matrix[2])};
    glm::vec3 scale{glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2])};
    if (glm::determinant(glm::mat3(matrix)) < 0.0f) {
        scale.x = -scale.x;
    }

    TransformComponents components;
    components.translation = FromGlm(glm::vec3(matrix[3]));
    components.scale = FromGlm(scale);
    // An axis scaled to nothing has no direction, so no rotation can be told.
    constexpr float Epsilon = 1e-12f;
    if (std::abs(scale.x) < Epsilon || std::abs(scale.y) < Epsilon || std::abs(scale.z) < Epsilon) {
        return components;
    }
    // Made orthonormal, which drops any shear.
    const glm::vec3 x = glm::normalize(axes[0] / scale.x);
    const glm::vec3 y = glm::normalize(axes[1] - glm::dot(axes[1], x) * x);
    const glm::vec3 z = glm::cross(x, y);
    components.rotation = FromGlm(glm::normalize(glm::quat_cast(glm::mat3(x, y, z))));
    return components;
}

} // namespace Spot
