#pragma once

// Backend-internal: what a Jolt simulation holds. Its functions are spread over the backend's sources by topic:
// bodies, queries and characters.

#include "spot/physics/jolt/jolt_contacts.h"
#include "spot/physics/jolt/jolt_layers.h"
#include "spot/physics/jolt/jolt_runtime.h"
#include "spot/physics/jolt/jolt_shapes.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace Spot {

namespace Detail {

inline constexpr JPH::uint MaxBodies = 16384;
inline constexpr JPH::uint MaxBodyPairs = 16384;
inline constexpr JPH::uint MaxContactConstraints = 8192;

} // namespace Detail

struct Physics3D::Impl {
    // Declared first, so the runtime outlives everything else.
    Detail::JoltRuntimeUser runtime;
    Detail::BroadPhaseLayerMap broadPhaseLayers;
    Detail::ObjectVsBroadPhaseFilter objectVsBroadPhaseFilter;
    Detail::LayerMasks layerMasks = Detail::MakeAllLayers();
    Detail::ObjectPairFilter objectPairFilter{layerMasks};
    Detail::ContactRecorder contacts;
    Detail::MeshShapeCache meshShapes;
    JPH::PhysicsSystem system;
    // By id; destroyed characters leave empty slots, which new ones take.
    std::vector<std::unique_ptr<JPH::CharacterVirtual>> characters;
    std::vector<int> characterLayers;
    // By id, as characters are.
    std::vector<JPH::Ref<JPH::TwoBodyConstraint>> joints;

    Impl();

    JPH::BodyInterface& GetBodies() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& GetBodies() const { return system.GetBodyInterface(); }
    JPH::CharacterVirtual& GetCharacter(CharacterId3D id) const { return *characters[static_cast<std::size_t>(id)]; }

    void WakeBodiesAround(JPH::BodyID body);

    // Query results, with the index of the shape the hit sub shape belongs to.
    QueryHit3D MakeHit(const JPH::BodyID& body, const JPH::SubShapeID& subShape) const;
    QueryHit3D MakeRayHit(const JPH::RRayCast& ray, const JPH::RayCastResult& result, float maxDistance) const;
    void Overlap(const JPH::Shape& shape, const JPH::RMat44& transform, const QueryFilter3D& filter,
                 std::vector<QueryHit3D>& hits) const;
};

} // namespace Spot
