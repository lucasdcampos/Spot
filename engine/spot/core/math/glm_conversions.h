#pragma once

// Engine-internal bridge to glm, which implements the heavier math. Public headers never include it, so glm
// stays a private dependency.

#include "spot/core/math/matrix.h"
#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Spot::Detail {

inline glm::vec3 ToGlm(Vector3 v) {
    return {v.x, v.y, v.z};
}

inline glm::vec4 ToGlm(Vector4 v) {
    return {v.x, v.y, v.z, v.w};
}

// Fields are assigned one by one because the order of glm's quaternion constructor depends on its configuration.
inline glm::quat ToGlm(const Quaternion& q) {
    glm::quat result;
    result.x = q.x;
    result.y = q.y;
    result.z = q.z;
    result.w = q.w;
    return result;
}

inline glm::mat4 ToGlm(const Matrix4& m) {
    glm::mat4 result;
    for (int column = 0; column < 4; ++column) {
        result[column] = ToGlm(m.columns[column]);
    }
    return result;
}

inline Vector3 FromGlm(const glm::vec3& v) {
    return {v.x, v.y, v.z};
}

inline Vector4 FromGlm(const glm::vec4& v) {
    return {v.x, v.y, v.z, v.w};
}

inline Quaternion FromGlm(const glm::quat& q) {
    return {q.x, q.y, q.z, q.w};
}

inline Matrix4 FromGlm(const glm::mat4& m) {
    Matrix4 result;
    for (int column = 0; column < 4; ++column) {
        result.columns[column] = FromGlm(m[column]);
    }
    return result;
}

} // namespace Spot::Detail
