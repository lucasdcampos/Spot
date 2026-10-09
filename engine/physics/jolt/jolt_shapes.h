#pragma once

// Backend-internal: Jolt shapes for the engine's.

#include "physics/jolt/jolt_common.h"

#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>

#include <cstdint>
#include <vector>

namespace Spot::Detail {

JPH::RefConst<JPH::Shape> CreateShape(const PhysicsShape& shape);
// One shape for a body of any number of them: a compound when there are several, and an empty shape without any.
JPH::RefConst<JPH::Shape> CreateBodyShape(const std::vector<PhysicsShape>& shapes);
// The index, in the shapes CreateBodyShape() was given, of the shape a sub shape of the body's shape belongs to.
std::uint32_t GetShapeIndex(const JPH::Shape& shape, const JPH::SubShapeID& subShape);

} // namespace Spot::Detail
