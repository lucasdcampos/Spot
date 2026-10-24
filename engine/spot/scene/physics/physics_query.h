#pragma once

#include "spot/core/math/vector.h"
#include "spot/scene/entity.h"

#include <cstdint>

namespace Spot {

class Collider;

// Which bodies a query sees.
struct QueryFilter {
    // A bit per layer whose bodies it sees; see Entity::GetLayer().
    std::uint32_t layerMask = 0xffffffffu;
    // Whether triggers count, as when looking for zones rather than for what blocks the way.
    bool includeTriggers = false;
    // An entity whose bodies the query goes through, such as the one asking.
    EntityId ignore = EntityId::Invalid;
};

// Where a ray or a cast shape met a body.
struct RaycastHit {
    EntityId entity = EntityId::Invalid;
    // The collider of the entity that was hit. Valid until the entity's colliders change, as references to components
    // are; the entity's id lasts.
    Collider* collider = nullptr;
    // In world space. For a cast shape, the point where it touched the body.
    Vector3 point = {};
    // The surface's normal at the point, toward the side the query came from.
    Vector3 normal = {};
    // How far the ray, or the cast shape, went from its origin before it met the body.
    float distance = 0.0f;
};

// A collider overlapping a shape, as found by PhysicsWorld::OverlapSphere() and OverlapBox().
struct OverlapHit {
    EntityId entity = EntityId::Invalid;
    // Valid until the entity's colliders change.
    Collider* collider = nullptr;
};

} // namespace Spot
