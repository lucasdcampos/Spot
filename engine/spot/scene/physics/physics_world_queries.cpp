#include "spot/scene/physics/physics_world.h"

#include "spot/render/debug_draw.h"
#include "spot/scene/scene.h"

#include <utility>

namespace Spot {

QueryFilter3D PhysicsWorld::MakeQueryFilter(const QueryFilter& filter) const {
    QueryFilter3D query{.layerMask = filter.layerMask, .includeTriggers = filter.includeTriggers};
    if (const auto ignored = m_bodies.find(filter.ignore); ignored != m_bodies.end()) {
        const Body& body = ignored->second;
        const BodyId3D character = body.character != CharacterId3D::Invalid
                                       ? m_simulation->GetCharacterBody(body.character)
                                       : BodyId3D::Invalid;
        query.ignored = {body.id, body.sensor, character};
    }
    return query;
}

bool PhysicsWorld::PrepareQuery() {
    RebuildChanged();
    return m_simulation && !m_bodies.empty();
}

RaycastHit PhysicsWorld::MakeHit(const QueryHit3D& hit) const {
    RaycastHit result{.point = hit.point, .normal = hit.normal, .distance = hit.distance};
    if (const auto owner = m_owners.find(hit.body); owner != m_owners.end()) {
        result.entity = owner->second.entity;
        result.collider = hit.shape < owner->second.colliders.size() ? owner->second.colliders[hit.shape] : nullptr;
    }
    return result;
}

std::vector<OverlapHit> PhysicsWorld::MakeOverlapHits() const {
    std::vector<OverlapHit> hits;
    hits.reserve(m_queryHits.size());
    for (const QueryHit3D& hit : m_queryHits) {
        const RaycastHit found = MakeHit(hit);
        hits.push_back({found.entity, found.collider});
    }
    return hits;
}

std::optional<RaycastHit> PhysicsWorld::Raycast(Vector3 origin, Vector3 direction, float maxDistance,
                                                const QueryFilter& filter) {
    QueryHit3D hit;
    std::optional<RaycastHit> result;
    if (PrepareQuery() && LengthSquared(direction) > 0.0f && maxDistance > 0.0f &&
        m_simulation->Raycast(origin, direction, maxDistance, MakeQueryFilter(filter), hit)) {
        result = MakeHit(hit);
    }
    RecordQuery({.kind = DebugQuery::Kind::Ray,
                 .origin = origin,
                 .end = origin + Normalize(direction) * maxDistance,
                 .radius = 0.0f,
                 .hit = result});
    return result;
}

std::vector<RaycastHit> PhysicsWorld::RaycastAll(Vector3 origin, Vector3 direction, float maxDistance,
                                                 const QueryFilter& filter) {
    std::vector<RaycastHit> hits;
    if (!PrepareQuery() || LengthSquared(direction) <= 0.0f || maxDistance <= 0.0f) {
        return hits;
    }
    m_simulation->RaycastAll(origin, direction, maxDistance, MakeQueryFilter(filter), m_queryHits);
    for (const QueryHit3D& hit : m_queryHits) {
        hits.push_back(MakeHit(hit));
    }
    RecordQuery({.kind = DebugQuery::Kind::Ray,
                 .origin = origin,
                 .end = origin + Normalize(direction) * maxDistance,
                 .radius = 0.0f,
                 .hit = hits.empty() ? std::nullopt : std::optional(hits.back())});
    return hits;
}

std::optional<RaycastHit> PhysicsWorld::SphereCast(Vector3 origin, float radius, Vector3 direction, float maxDistance,
                                                   const QueryFilter& filter) {
    QueryHit3D hit;
    std::optional<RaycastHit> result;
    if (PrepareQuery() && LengthSquared(direction) > 0.0f && maxDistance >= 0.0f &&
        m_simulation->SphereCast(origin, radius, direction, maxDistance, MakeQueryFilter(filter), hit)) {
        result = MakeHit(hit);
    }
    RecordQuery({.kind = DebugQuery::Kind::Ray,
                 .origin = origin,
                 .end = origin + Normalize(direction) * maxDistance,
                 .radius = radius,
                 .hit = result});
    return result;
}

std::vector<OverlapHit> PhysicsWorld::OverlapSphere(Vector3 center, float radius, const QueryFilter& filter) {
    if (!PrepareQuery()) {
        return {};
    }
    m_simulation->OverlapSphere(center, radius, MakeQueryFilter(filter), m_queryHits);
    RecordQuery({.kind = DebugQuery::Kind::Sphere,
                 .origin = center,
                 .radius = radius,
                 .hit = m_queryHits.empty() ? std::nullopt : std::optional(RaycastHit{})});
    return MakeOverlapHits();
}

std::vector<OverlapHit> PhysicsWorld::OverlapBox(Vector3 center, Vector3 size, const Quaternion& rotation,
                                                 const QueryFilter& filter) {
    if (!PrepareQuery()) {
        return {};
    }
    m_simulation->OverlapBox(center, size * 0.5f, Normalize(rotation), MakeQueryFilter(filter), m_queryHits);
    RecordQuery({.kind = DebugQuery::Kind::Box,
                 .origin = center,
                 .size = size,
                 .rotation = rotation,
                 .radius = 0.0f,
                 .hit = m_queryHits.empty() ? std::nullopt : std::optional(RaycastHit{})});
    return MakeOverlapHits();
}

void PhysicsWorld::SetDebugDraw(PhysicsDebugFlags flags) {
    m_debugFlags = flags;
    if (!HasFlag(flags, PhysicsDebugFlags::Queries)) {
        m_debugQueries.clear();
    }
}

void PhysicsWorld::RecordQuery(DebugQuery query) {
    if (HasFlag(m_debugFlags, PhysicsDebugFlags::Queries)) {
        m_debugQueries.push_back(std::move(query));
    }
}

void PhysicsWorld::OnBeforeRender() {
    if (m_debugFlags == PhysicsDebugFlags::None) {
        return;
    }
    DebugLines& lines = DebugDraw::GetQueue();
    if (HasFlag(m_debugFlags, PhysicsDebugFlags::Colliders)) {
        DrawColliders(GetScene(), lines);
    }
    if (HasFlag(m_debugFlags, PhysicsDebugFlags::Contacts)) {
        const DebugDrawOptions options{.color = {1.0f, 0.2f, 0.2f, 1.0f}, .depthTest = false};
        for (const PairContact& contact : m_previous) {
            for (const Vector3 point : contact.collision.GetPoints()) {
                lines.Cross(point, 0.1f, options);
                // Toward the first entity of the pair, the way it is pushed.
                lines.Arrow(point, point + contact.collision.normal * 0.3f, options);
            }
        }
    }
    if (HasFlag(m_debugFlags, PhysicsDebugFlags::Queries)) {
        const DebugDrawOptions missed{.color = {0.6f, 0.6f, 0.6f, 1.0f}};
        const DebugDrawOptions found{.color = {1.0f, 0.3f, 0.9f, 1.0f}};
        for (const DebugQuery& query : m_debugQueries) {
            const DebugDrawOptions& options = query.hit ? found : missed;
            switch (query.kind) {
            case DebugQuery::Kind::Ray:
                lines.Line(query.origin, query.hit ? query.hit->point : query.end, options);
                if (query.radius > 0.0f) {
                    lines.Sphere(query.origin, query.radius, options);
                    lines.Sphere(query.hit ? query.origin + Normalize(query.end - query.origin) * query.hit->distance
                                           : query.end,
                                 query.radius, options);
                }
                if (query.hit) {
                    lines.Cross(query.hit->point, 0.15f, found);
                    lines.Arrow(query.hit->point, query.hit->point + query.hit->normal * 0.4f, found);
                }
                break;
            case DebugQuery::Kind::Sphere:
                lines.Sphere(query.origin, query.radius, options);
                break;
            case DebugQuery::Kind::Box:
                lines.Box(query.origin, query.size, query.rotation, options);
                break;
            }
        }
        m_debugQueries.clear();
    }
}

} // namespace Spot
