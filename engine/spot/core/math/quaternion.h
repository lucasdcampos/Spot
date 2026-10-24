#pragma once

#include "spot/core/math/vector.h"

namespace Spot {

// A rotation. Angles are in radians, and a positive angle turns counterclockwise when the axis points toward the
// viewer (right-handed).
struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static constexpr Quaternion Identity() { return {}; }
    static Quaternion FromAxisAngle(Vector3 axis, float angle);
    // Pitch around X, yaw around Y and roll around Z.
    static Quaternion FromEuler(Vector3 angles);
    // Turns -Z, the way cameras look, toward the direction, keeping up as close to up as it allows. When the two are
    // parallel, another up is picked.
    static Quaternion LookRotation(Vector3 direction, Vector3 up = {0.0f, 1.0f, 0.0f});

    friend constexpr bool operator==(const Quaternion&, const Quaternion&) = default;
};

// Applies b first, then a.
Quaternion operator*(const Quaternion& a, const Quaternion& b);
Vector3 operator*(const Quaternion& rotation, Vector3 v);

// The pitch, yaw and roll that FromEuler() turns back into this rotation. For a rotation around Z alone, as in 2D,
// z is its angle.
Vector3 ToEuler(const Quaternion& q);

Quaternion Normalize(const Quaternion& q);
Quaternion Inverse(const Quaternion& q);
Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);

} // namespace Spot
