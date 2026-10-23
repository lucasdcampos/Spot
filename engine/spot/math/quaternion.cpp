#include "spot/math/quaternion.h"

#include "spot/math/glm_conversions.h"

#include <cmath>

namespace Spot {

using Detail::FromGlm;
using Detail::ToGlm;

Quaternion Quaternion::FromAxisAngle(Vector3 axis, float angle) {
    return FromGlm(glm::angleAxis(angle, glm::normalize(ToGlm(axis))));
}

Quaternion Quaternion::FromEuler(Vector3 angles) {
    return FromGlm(glm::quat(ToGlm(angles)));
}

Quaternion Quaternion::LookRotation(Vector3 direction, Vector3 up) {
    const Vector3 forward = Normalize(direction);
    if (LengthSquared(forward) == 0.0f) {
        return {};
    }
    if (LengthSquared(Cross(forward, Normalize(up))) < 1e-6f) {
        up = std::abs(forward.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{0.0f, 0.0f, 1.0f};
    }
    return FromGlm(glm::quatLookAtRH(ToGlm(forward), ToGlm(up)));
}

Quaternion operator*(const Quaternion& a, const Quaternion& b) {
    return FromGlm(ToGlm(a) * ToGlm(b));
}

Vector3 operator*(const Quaternion& rotation, Vector3 v) {
    return FromGlm(ToGlm(rotation) * ToGlm(v));
}

Vector3 ToEuler(const Quaternion& q) {
    return FromGlm(glm::eulerAngles(ToGlm(q)));
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
