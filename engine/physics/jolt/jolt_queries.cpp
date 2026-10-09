#include "physics/jolt/jolt_shapes.h"
#include "physics/jolt/jolt_simulation_impl.h"

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include <algorithm>

namespace Spot::Detail {

PhysicsQueryHit PhysicsSimulation::Impl::MakeHit(const JPH::BodyID& body, const JPH::SubShapeID& subShape) const {
    const JPH::RefConst<JPH::Shape> shape = GetBodies().GetShape(body);
    return {.body = FromJolt(body), .shape = shape ? GetShapeIndex(*shape, subShape) : 0};
}

PhysicsQueryHit PhysicsSimulation::Impl::MakeRayHit(const JPH::RRayCast& ray, const JPH::RayCastResult& result,
                                                    float maxDistance) const {
    PhysicsQueryHit hit = MakeHit(result.mBodyID, result.mSubShapeID2);
    const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
    hit.point = FromJolt(JPH::Vec3(point));
    hit.normal = FromJolt(
        GetBodies().GetTransformedShape(result.mBodyID).GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
    hit.distance = result.mFraction * maxDistance;
    return hit;
}

void PhysicsSimulation::Impl::Overlap(const JPH::Shape& shape, const JPH::RMat44& transform,
                                      const PhysicsQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const {
    hits.clear();
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    system.GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sOne(), transform, JPH::CollideShapeSettings(),
                                              JPH::RVec3::sZero(), collector, {}, layers, bodies);
    for (const JPH::CollideShapeResult& result : collector.mHits) {
        const PhysicsQueryHit hit = MakeHit(result.mBodyID2, result.mSubShapeID2);
        const bool seen = std::ranges::any_of(
            hits, [&hit](const PhysicsQueryHit& other) { return other.body == hit.body && other.shape == hit.shape; });
        if (!seen) {
            hits.push_back(hit);
        }
    }
}

bool PhysicsSimulation::CastRay(Vector3 origin, Vector3 direction, float maxDistance, const PhysicsQueryFilter& filter,
                                PhysicsQueryHit& hit) const {
    const JPH::RRayCast ray{ToJolt(origin), ToJolt(direction) * maxDistance};
    JPH::RayCastResult result;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    if (!m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, {}, layers, bodies)) {
        return false;
    }
    hit = m_impl->MakeRayHit(ray, result, maxDistance);
    return true;
}

void PhysicsSimulation::CastRayAll(Vector3 origin, Vector3 direction, float maxDistance,
                                   const PhysicsQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const {
    hits.clear();
    const JPH::RRayCast ray{ToJolt(origin), ToJolt(direction) * maxDistance};
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    m_impl->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), collector, {}, layers, bodies);
    collector.Sort();
    for (const JPH::RayCastResult& result : collector.mHits) {
        hits.push_back(m_impl->MakeRayHit(ray, result, maxDistance));
    }
}

bool PhysicsSimulation::CastSphere(Vector3 origin, float radius, Vector3 direction, float maxDistance,
                                   const PhysicsQueryFilter& filter, PhysicsQueryHit& hit) const {
    JPH::SphereShape sphere(std::max(radius, MinShapeSize));
    // On the stack, so Jolt's references must not free it.
    sphere.SetEmbedded();
    const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
        &sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(ToJolt(origin)), ToJolt(direction) * maxDistance);
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    m_impl->system.GetNarrowPhaseQuery().CastShape(cast, JPH::ShapeCastSettings(), JPH::RVec3::sZero(), collector, {},
                                                   layers, bodies);
    if (!collector.HadHit()) {
        return false;
    }
    const JPH::ShapeCastResult& result = collector.mHit;
    hit = m_impl->MakeHit(result.mBodyID2, result.mSubShapeID2);
    hit.point = FromJolt(result.mContactPointOn2);
    // The penetration axis pushes the body out of the sphere; the normal faces the sphere.
    hit.normal = FromJolt(-result.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()));
    hit.distance = result.mFraction * maxDistance;
    return true;
}

void PhysicsSimulation::OverlapSphere(Vector3 center, float radius, const PhysicsQueryFilter& filter,
                                      std::vector<PhysicsQueryHit>& hits) const {
    JPH::SphereShape sphere(std::max(radius, MinShapeSize));
    sphere.SetEmbedded();
    m_impl->Overlap(sphere, JPH::RMat44::sTranslation(ToJolt(center)), filter, hits);
}

void PhysicsSimulation::OverlapBox(Vector3 center, Vector3 halfExtents, const Quaternion& rotation,
                                   const PhysicsQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const {
    const JPH::Vec3 extents = JPH::Vec3::sMax(ToJolt(halfExtents), JPH::Vec3::sReplicate(MinShapeSize));
    JPH::BoxShape box(extents, std::min(JPH::cDefaultConvexRadius, extents.ReduceMin()));
    box.SetEmbedded();
    m_impl->Overlap(box, JPH::RMat44::sRotationTranslation(ToJolt(rotation), ToJolt(center)), filter, hits);
}

} // namespace Spot::Detail
