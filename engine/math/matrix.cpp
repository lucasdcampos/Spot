#include "math/matrix.h"

#include "math/glm_conversions.h"

#include <glm/gtc/matrix_transform.hpp>

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

} // namespace Spot
