#pragma once

#include "spot/core/vector.h"
#include "spot/scene/entity.h"

#include <array>
#include <cstdint>
#include <span>

namespace Spot {

// Two solid bodies touching, as seen by one of their entities; see Component::OnCollisionEnter().
struct Collision {
    static constexpr std::size_t MaxPoints = 4;

    // The entity of the other body. It may be destroyed by the time an exit is reported.
    EntityId other = EntityId::Invalid;
    // Where the bodies touch, in world space.
    std::array<Vector3, MaxPoints> points = {};
    std::uint32_t pointCount = 0;
    // A unit vector from the other body toward this one: the way this one is pushed out.
    Vector3 normal = {};
    // This body's velocity minus the other's, at the first point. Its part along the normal is how fast they met.
    Vector3 relativeVelocity = {};
    // An estimate of how hard they hit: the impulse that stops them moving into each other, in newton seconds, from
    // their masses and how fast they met. Close to 0 for bodies resting on each other.
    float impulse = 0.0f;

    std::span<const Vector3> GetPoints() const { return {points.data(), pointCount}; }
};

// A body and a trigger overlapping, as seen by one of their entities; see Component::OnTriggerEnter().
struct TriggerContact {
    // The entity on the other side: the trigger's, for the entity that entered it, or the one that entered, for the
    // trigger's. It may be destroyed by the time an exit is reported.
    EntityId other = EntityId::Invalid;
    // Whether this entity's trigger is the one overlapped, rather than the other's.
    bool isTrigger = false;
};

} // namespace Spot
