#include "spot/physics/jolt/jolt_shapes.h"
#include "spot/physics/jolt/jolt_simulation_impl.h"

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include <algorithm>

namespace Spot {

using namespace Detail;

QueryHit3D Physics3D::Impl::MakeHit(const JPH::BodyID& body, const JPH::SubShapeID& subShape) const {
    const JPH::RefConst<JPH::Shape> shape = GetBodies().GetShape(body);
    return {.body = FromJolt(body), .shape = shape ? GetShapeIndex(*shape, subShape) : 0};
}

QueryHit3D Physics3D::Impl::MakeRayHit(const JPH::RRayCast& ray, const JPH::RayCastResult& result,
                                       float maxDistance) const {
    QueryHit3D hit = MakeHit(result.mBodyID, result.mSubShapeID2);
    const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
    hit.point = FromJolt(JPH::Vec3(point));
    hit.normal = FromJolt(
        GetBodies().GetTransformedShape(result.mBodyID).GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
    hit.distance = result.mFraction * maxDistance;
    return hit;
}

void Physics3D::Impl::Overlap(const JPH::Shape& shape, const JPH::RMat44& transform, const QueryFilter3D& filter,
                              std::vector<QueryHit3D>& hits) const {
    hits.clear();
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    system.GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sOne(), transform, JPH::CollideShapeSettings(),
                                              JPH::RVec3::sZero(), collector, {}, layers, bodies);
    for (const JPH::CollideShapeResult& result : collector.mHits) {
        const QueryHit3D hit = MakeHit(result.mBodyID2, result.mSubShapeID2);
        const bool seen = std::ranges::any_of(
            hits, [&hit](const QueryHit3D& other) { return other.body == hit.body && other.shape == hit.shape; });
        if (!seen) {
            hits.push_back(hit);
        }
    }
}

bool Physics3D::Raycast(Vector3 origin, Vector3 direction, float maxDistance, const QueryFilter3D& filter,
                        QueryHit3D& hit) const {
    if (LengthSquared(direction) <= 0.0f || maxDistance <= 0.0f) {
        return false;
    }
    const JPH::RRayCast ray{ToJolt(origin), ToJolt(Normalize(direction)) * maxDistance};
    JPH::RayCastResult result;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    if (!m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, {}, layers, bodies)) {
        return false;
    }
    hit = m_impl->MakeRayHit(ray, result, maxDistance);
    return true;
}

void Physics3D::RaycastAll(Vector3 origin, Vector3 direction, float maxDistance, const QueryFilter3D& filter,
                           std::vector<QueryHit3D>& hits) const {
    hits.clear();
    if (LengthSquared(direction) <= 0.0f || maxDistance <= 0.0f) {
        return;
    }
    const JPH::RRayCast ray{ToJolt(origin), ToJolt(Normalize(direction)) * maxDistance};
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    m_impl->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), collector, {}, layers, bodies);
    collector.Sort();
    for (const JPH::RayCastResult& result : collector.mHits) {
        hits.push_back(m_impl->MakeRayHit(ray, result, maxDistance));
    }
}

bool Physics3D::SphereCast(Vector3 origin, float radius, Vector3 direction, float maxDistance,
                           const QueryFilter3D& filter, QueryHit3D& hit) const {
    if (LengthSquared(direction) <= 0.0f || maxDistance < 0.0f) {
        return false;
    }
    JPH::SphereShape sphere(std::max(radius, MinShapeSize));
    // On the stack, so Jolt's references must not free it.
    sphere.SetEmbedded();
    const JPH::RShapeCast cast =
        JPH::RShapeCast::sFromWorldTransform(&sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(ToJolt(origin)),
                                             ToJolt(Normalize(direction)) * maxDistance);
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

void Physics3D::OverlapSphere(Vector3 center, float radius, const QueryFilter3D& filter,
                              std::vector<QueryHit3D>& hits) const {
    JPH::SphereShape sphere(std::max(radius, MinShapeSize));
    sphere.SetEmbedded();
    m_impl->Overlap(sphere, JPH::RMat44::sTranslation(ToJolt(center)), filter, hits);
}

void Physics3D::OverlapBox(Vector3 center, Vector3 halfExtents, const Quaternion& rotation, const QueryFilter3D& filter,
                           std::vector<QueryHit3D>& hits) const {
    const JPH::Vec3 extents = JPH::Vec3::sMax(ToJolt(halfExtents), JPH::Vec3::sReplicate(MinShapeSize));
    JPH::BoxShape box(extents, std::min(JPH::cDefaultConvexRadius, extents.ReduceMin()));
    box.SetEmbedded();
    m_impl->Overlap(box, JPH::RMat44::sRotationTranslation(ToJolt(rotation), ToJolt(center)), filter, hits);
}

} // namespace Spot
