#include "physics/jolt/jolt_shapes.h"

#include "core/log.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>

namespace Spot::Detail {

JPH::RefConst<JPH::Shape> CreateShape(const PhysicsShape& shape) {
    const float radius = std::max(shape.radius, MinShapeSize);
    switch (shape.kind) {
    case PhysicsShapeKind::Box: {
        const JPH::Vec3 halfExtents = JPH::Vec3::sMax(ToJolt(shape.halfExtents), JPH::Vec3::sReplicate(MinShapeSize));
        // The rounding of the edges cannot exceed the box.
        return new JPH::BoxShape(halfExtents, std::min(JPH::cDefaultConvexRadius, halfExtents.ReduceMin()));
    }
    case PhysicsShapeKind::Sphere:
        return new JPH::SphereShape(radius);
    case PhysicsShapeKind::Capsule:
        if (shape.halfHeight <= 0.0f) {
            return new JPH::SphereShape(radius);
        }
        return new JPH::CapsuleShape(shape.halfHeight, radius);
    }
    return new JPH::SphereShape(radius);
}

JPH::RefConst<JPH::Shape> CreateBodyShape(const std::vector<PhysicsShape>& shapes) {
    if (shapes.empty()) {
        return new JPH::EmptyShape();
    }
    if (shapes.size() == 1) {
        const PhysicsShape& shape = shapes.front();
        if (shape.center == Vector3{}) {
            return CreateShape(shape);
        }
        return new JPH::RotatedTranslatedShape(ToJolt(shape.center), JPH::Quat::sIdentity(), CreateShape(shape));
    }

    JPH::StaticCompoundShapeSettings compound;
    for (const PhysicsShape& shape : shapes) {
        compound.AddShape(ToJolt(shape.center), JPH::Quat::sIdentity(), CreateShape(shape));
    }
    const JPH::ShapeSettings::ShapeResult result = compound.Create();
    if (result.HasError()) {
        Log::GetEngineLogger().Error("Could not combine the shapes of a body: {}", result.GetError().c_str());
        return new JPH::EmptyShape();
    }
    return result.Get();
}

std::uint32_t GetShapeIndex(const JPH::Shape& shape, const JPH::SubShapeID& subShape) {
    if (shape.GetSubType() == JPH::EShapeSubType::StaticCompound) {
        JPH::SubShapeID remainder;
        return static_cast<const JPH::CompoundShape&>(shape).GetSubShapeIndexFromID(subShape, remainder);
    }
    return 0;
}

} // namespace Spot::Detail
