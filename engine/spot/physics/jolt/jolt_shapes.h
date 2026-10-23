#pragma once

// Backend-internal: Jolt shapes for the engine's.

#include "spot/physics/jolt/jolt_common.h"

#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Spot::Detail {

// The shapes built from meshes, kept while their mesh lives, as building them takes long and many bodies often share a
// mesh.
class MeshShapeCache {
public:
    // Null, after logging why, when the mesh has no shape.
    JPH::RefConst<JPH::Shape> Get(const std::shared_ptr<const MeshData>& mesh, bool convex);
    std::size_t GetCount() const { return m_shapes.size(); }

private:
    struct Shapes {
        std::weak_ptr<const MeshData> mesh;
        JPH::RefConst<JPH::Shape> triangles;
        JPH::RefConst<JPH::Shape> hull;
    };

    std::unordered_map<const MeshData*, Shapes> m_shapes;
};

JPH::RefConst<JPH::Shape> CreateShape(const PhysicsShape& shape, MeshShapeCache& meshes);
// One shape for a body of any number of them: a compound when there are several, and an empty shape without any.
JPH::RefConst<JPH::Shape> CreateBodyShape(const std::vector<PhysicsShape>& shapes, MeshShapeCache& meshes);
// The index, in the shapes CreateBodyShape() was given, of the shape a sub shape of the body's shape belongs to.
std::uint32_t GetShapeIndex(const JPH::Shape& shape, const JPH::SubShapeID& subShape);

} // namespace Spot::Detail
