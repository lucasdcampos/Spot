#pragma once

#include "spot/math/vector.h"
#include "spot/scene/entity.h"

#include <array>
#include <span>

namespace Spot {
struct Collision2D {
    EntityId other = EntityId::Invalid;
    std::array<Vector2, 2> points = {};
    std::uint32_t pointCount = 0;
    Vector2 normal = {};
    Vector2 relativeVelocity = {};
    float impulse = 0.0f;
    std::span<const Vector2> GetPoints() const { return {points.data(), pointCount}; }
};
struct TriggerContact2D {
    EntityId other = EntityId::Invalid;
    bool isTrigger = false;
};
} // namespace Spot
