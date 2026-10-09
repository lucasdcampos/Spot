#include "physics/jolt/jolt_shapes.h"

#include "core/log.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>

namespace Spot::Detail {

namespace {

JPH::RefConst<JPH::Shape> BuildMeshShape(const MeshData& mesh, bool convex) {
    JPH::ShapeSettings::ShapeResult result;
    if (convex) {
        JPH::Array<JPH::Vec3> points;
        points.reserve(mesh.vertices.size());
        for (const MeshVertex& vertex : mesh.vertices) {
            points.push_back(ToJolt(vertex.position));
        }
        result = JPH::ConvexHullShapeSettings(points).Create();
    } else {
        JPH::VertexList vertices;
        vertices.reserve(mesh.vertices.size());
        for (const MeshVertex& vertex : mesh.vertices) {
            vertices.push_back(JPH::Float3(vertex.position.x, vertex.position.y, vertex.position.z));
        }
        JPH::IndexedTriangleList triangles;
        triangles.reserve(mesh.indices.size() / 3);
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            triangles.push_back(JPH::IndexedTriangle(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]));
        }
        result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
    }
    if (result.HasError()) {
        Log::GetEngineLogger().Error("Could not build a {} from a mesh of {} vertices: {}",
                                     convex ? "convex hull" : "triangle mesh", mesh.vertices.size(),
                                     result.GetError().c_str());
        return nullptr;
    }
    return result.Get();
}

} // namespace

JPH::RefConst<JPH::Shape> MeshShapeCache::Get(const std::shared_ptr<const MeshData>& mesh, bool convex) {
    if (!mesh) {
        return nullptr;
    }
    // Entries of meshes that are gone go first, so the cache only holds what lives, and so a new mesh at the address
    // of a freed one never gets its shapes.
    std::erase_if(m_shapes, [](const auto& entry) { return entry.second.mesh.expired(); });
    Shapes& shapes = m_shapes[mesh.get()];
    shapes.mesh = mesh;
    JPH::RefConst<JPH::Shape>& shape = convex ? shapes.hull : shapes.triangles;
    if (!shape) {
        shape = BuildMeshShape(*mesh, convex);
    }
    return shape;
}

JPH::RefConst<JPH::Shape> CreateShape(const PhysicsShape& shape, MeshShapeCache& meshes) {
    const float radius = std::max(shape.radius, MinShapeSize);
    switch (shape.kind) {
    case PhysicsShapeKind::Mesh:
    case PhysicsShapeKind::ConvexHull: {
        JPH::RefConst<JPH::Shape> built = meshes.Get(shape.mesh, shape.kind == PhysicsShapeKind::ConvexHull);
        if (!built) {
            return new JPH::EmptyShape();
        }
        const JPH::Vec3 scale = ToJolt(shape.meshScale);
        if (scale.IsClose(JPH::Vec3::sOne())) {
            return built;
        }
        return new JPH::ScaledShape(built, built->MakeScaleValid(scale));
    }
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

JPH::RefConst<JPH::Shape> CreateBodyShape(const std::vector<PhysicsShape>& shapes, MeshShapeCache& meshes) {
    if (shapes.empty()) {
        return new JPH::EmptyShape();
    }
    if (shapes.size() == 1) {
        const PhysicsShape& shape = shapes.front();
        if (shape.center == Vector3{}) {
            return CreateShape(shape, meshes);
        }
        return new JPH::RotatedTranslatedShape(ToJolt(shape.center), JPH::Quat::sIdentity(),
                                               CreateShape(shape, meshes));
    }

    JPH::StaticCompoundShapeSettings compound;
    for (const PhysicsShape& shape : shapes) {
        compound.AddShape(ToJolt(shape.center), JPH::Quat::sIdentity(), CreateShape(shape, meshes));
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
