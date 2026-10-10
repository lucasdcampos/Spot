#include "prop_drive.h"

#include <algorithm>
#include <cmath>

namespace PropDrive {

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

Spot::Vector3 ClampLength(Spot::Vector3 v, float maxLength) {
    const float length = Spot::Length(v);
    return length > maxLength ? v * (maxLength / length) : v;
}

} // namespace

void Toward(Spot::Rigidbody& body, Spot::Vector3 point, Spot::Vector3 target, const Spot::Quaternion& rotation,
            float deltaTime, float maxSpeed) {
    // Closing a fifth of the gap each step damps the motion, so the prop settles rather than overshooting.
    const float rate = 0.2f / deltaTime;
    body.SetLinearVelocity(ClampLength((target - point) * rate, maxSpeed));

    const Spot::Quaternion current = body.GetEntity().GetTransform().GetWorldRotation();
    Spot::Quaternion delta = rotation * Spot::Quaternion{-current.x, -current.y, -current.z, current.w};
    // The shorter way around.
    if (delta.w < 0.0f) {
        delta = {-delta.x, -delta.y, -delta.z, -delta.w};
    }
    const Spot::Vector3 axis{delta.x, delta.y, delta.z};
    const float sine = Spot::Length(axis);
    const float angle = 2.0f * std::atan2(sine, delta.w);
    const Spot::Vector3 angular = sine > 1e-5f ? axis * (angle / sine) : Spot::Vector3{};
    body.SetAngularVelocity(ClampLength(angular * rate, 20.0f));
}

Spot::Quaternion GetFacing(float yaw) {
    return Spot::Quaternion::FromAxisAngle(Up, yaw);
}

Spot::Quaternion TurnByLook(const Spot::Quaternion& rotation, Spot::Vector2 look, float sensitivity) {
    const Spot::Quaternion yaw = Spot::Quaternion::FromAxisAngle(Up, look.x * sensitivity);
    const Spot::Quaternion pitch = Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, -look.y * sensitivity);
    return Spot::Normalize(yaw * pitch * rotation);
}

} // namespace PropDrive
