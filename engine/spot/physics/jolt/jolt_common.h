#pragma once

// Backend-internal: conversions between the engine's types and Jolt's, and how the entities' layers become Jolt's
// object layers. Only the Jolt backend includes it.

#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"
#include "spot/physics/physics_3d.h"

// Jolt.h comes before every other Jolt header.
#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include <array>
#include <cstdint>

namespace Spot::Detail {

// Smaller shapes make the simulation unstable.
inline constexpr float MinShapeSize = 0.001f;

inline JPH::Vec3 ToJolt(Vector3 v) {
    return {v.x, v.y, v.z};
}

inline JPH::Quat ToJolt(const Quaternion& q) {
    return {q.x, q.y, q.z, q.w};
}

inline Vector3 FromJolt(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

inline Quaternion FromJolt(JPH::QuatArg q) {
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

inline JPH::BodyID ToJolt(BodyId3D body) {
    return JPH::BodyID(static_cast<JPH::uint32>(body));
}

inline BodyId3D FromJolt(const JPH::BodyID& body) {
    return body.IsInvalid() ? BodyId3D::Invalid : static_cast<BodyId3D>(body.GetIndexAndSequenceNumber());
}

inline JPH::EMotionType ToJolt(BodyMotion3D motion) {
    switch (motion) {
    case BodyMotion3D::Static:
        return JPH::EMotionType::Static;
    case BodyMotion3D::Kinematic:
        return JPH::EMotionType::Kinematic;
    case BodyMotion3D::Dynamic:
        return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

inline JPH::Vec3 ClampLength(JPH::Vec3Arg v, float maxLength) {
    // Below the limit by a margin, which rounding cannot cross.
    const float length = v.Length();
    return length > maxLength ? v * (0.999f * maxLength / length) : v;
}

// Jolt's object layers pair the entity's layer with whether the body moves: the layer times two, plus one for moving
// bodies.
inline JPH::ObjectLayer ToObjectLayer(int layer, bool moving) {
    return static_cast<JPH::ObjectLayer>(layer * 2 + (moving ? 1 : 0));
}

inline bool IsMoving(JPH::ObjectLayer layer) {
    return (layer & 1) != 0;
}

inline int GetEntityLayer(JPH::ObjectLayer layer) {
    return layer / 2;
}

// For each layer, a bit per layer it collides with.
using LayerMasks = std::array<std::uint32_t, 32>;

inline LayerMasks MakeAllLayers() {
    LayerMasks masks;
    masks.fill(0xffffffffu);
    return masks;
}

} // namespace Spot::Detail
