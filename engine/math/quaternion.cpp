#include "math/quaternion.h"

#include "math/glm_conversions.h"

namespace Spot {

using Detail::FromGlm;
using Detail::ToGlm;

Quaternion Quaternion::FromAxisAngle(Vector3 axis, float angle) {
    return FromGlm(glm::angleAxis(angle, glm::normalize(ToGlm(axis))));
}

Quaternion Quaternion::FromEuler(Vector3 angles) {
    return FromGlm(glm::quat(ToGlm(angles)));
}

Quaternion operator*(const Quaternion& a, const Quaternion& b) {
    return FromGlm(ToGlm(a) * ToGlm(b));
}

Vector3 operator*(const Quaternion& rotation, Vector3 v) {
    return FromGlm(ToGlm(rotation) * ToGlm(v));
}

Quaternion Normalize(const Quaternion& q) {
    return FromGlm(glm::normalize(ToGlm(q)));
}

Quaternion Inverse(const Quaternion& q) {
    return FromGlm(glm::inverse(ToGlm(q)));
}

Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) {
    return FromGlm(glm::slerp(ToGlm(a), ToGlm(b), t));
}

} // namespace Spot
