#include "spot/physics/physics_2d.h"

#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Spot {
namespace {
b2Vec2 ToNative(Vector2 v) {
    return {v.x, v.y};
}
Vector2 FromNative(b2Vec2 v) {
    return {v.x, v.y};
}
bool Finite(Vector2 v) {
    return std::isfinite(v.x) && std::isfinite(v.y);
}
} // namespace

struct Physics2D::Impl {
    struct Body {
        BodyId2D id;
        b2BodyId native;
        int layer;
        std::vector<b2ShapeId> shapes;
    };
    b2WorldId world;
    std::uint64_t nextId = 1;
    std::array<std::uint32_t, 32> masks;
    std::unordered_map<BodyId2D, std::unique_ptr<Body>> bodies;
    std::vector<b2ContactData> contactBuffer;
    std::vector<b2ShapeId> overlapBuffer;

    Body* Find(BodyId2D id) const {
        auto found = bodies.find(id);
        return found == bodies.end() ? nullptr : found->second.get();
    }
    static Body& Owner(b2ShapeId shape) { return *static_cast<Body*>(b2Body_GetUserData(b2Shape_GetBody(shape))); }
    struct Query {
        const QueryFilter2D& filter;
        std::vector<QueryHit2D>& hits;
        float distance;
        bool Accept(b2ShapeId shape) const {
            const Body& body = Owner(shape);
            return body.id != filter.ignore && (filter.layerMask & (1u << body.layer)) &&
                   (filter.includeTriggers || !b2Shape_IsSensor(shape));
        }
        QueryHit2D Hit(b2ShapeId shape) const {
            const Body& body = Owner(shape);
            return {
                .body = body.id,
                .shape = static_cast<std::uint32_t>(
                    std::ranges::find_if(body.shapes, [shape](b2ShapeId other) { return B2_ID_EQUALS(shape, other); }) -
                    body.shapes.begin())};
        }
        static bool Overlap(b2ShapeId shape, void* context) {
            auto& query = *static_cast<Query*>(context);
            if (query.Accept(shape))
                query.hits.push_back(query.Hit(shape));
            return true;
        }
        static float Cast(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* context) {
            auto& query = *static_cast<Query*>(context);
            if (query.Accept(shape)) {
                auto hit = query.Hit(shape);
                hit.point = FromNative(point);
                hit.normal = FromNative(normal);
                hit.distance = fraction * query.distance;
                query.hits.push_back(hit);
            }
            return 1.0f;
        }
    };
};

Physics2D::Physics2D() : m_impl(std::make_unique<Impl>()) {
    auto def = b2DefaultWorldDef();
    def.gravity = {0.0f, -9.81f};
    m_impl->world = b2CreateWorld(&def);
    m_impl->masks.fill(0xffffffffu);
}
Physics2D::~Physics2D() {
    b2DestroyWorld(m_impl->world);
}
void Physics2D::SetGravity(Vector2 gravity) {
    if (Finite(gravity))
        b2World_SetGravity(m_impl->world, ToNative(gravity));
}
void Physics2D::SetLayerMasks(const std::array<std::uint32_t, 32>& masks) {
    m_impl->masks = masks;
    for (auto& [id, body] : m_impl->bodies) {
        for (auto shape : body->shapes) {
            auto filter = b2DefaultFilter();
            filter.categoryBits = std::uint64_t{1} << body->layer;
            filter.maskBits = masks[static_cast<std::size_t>(body->layer)];
            b2Shape_SetFilter(shape, filter);
        }
    }
}
BodyId2D Physics2D::CreateBody(const BodyDesc2D& desc) {
    if (!Finite(desc.position) || !Finite(desc.linearVelocity) || !std::isfinite(desc.angle) ||
        !std::isfinite(desc.angularVelocity) || !std::isfinite(desc.mass) || desc.mass <= 0.0f ||
        !std::isfinite(desc.gravityScale) || !std::isfinite(desc.linearDamping) || desc.linearDamping < 0.0f ||
        !std::isfinite(desc.angularDamping) || desc.angularDamping < 0.0f || desc.layer < 0 || desc.layer >= 32) {
        Log::GetEngineLogger().Error("Physics2D: invalid body description");
        return BodyId2D::Invalid;
    }
    for (const auto& shape : desc.shapes) {
        if (!Finite(shape.center) || !Finite(shape.size) || shape.size.x <= 0.0f || shape.size.y <= 0.0f ||
            !std::isfinite(shape.radius) || shape.radius <= 0.0f || !std::isfinite(shape.height) ||
            !std::isfinite(shape.friction) || shape.friction < 0.0f || !std::isfinite(shape.restitution) ||
            shape.restitution < 0.0f) {
            Log::GetEngineLogger().Error("Physics2D: invalid collider dimensions or material");
            return BodyId2D::Invalid;
        }
        if (shape.kind == ShapeKind2D::Polygon) {
            if (shape.vertices.size() < 3 || shape.vertices.size() > B2_MAX_POLYGON_VERTICES ||
                !std::ranges::all_of(shape.vertices, Finite)) {
                Log::GetEngineLogger().Error("Physics2D: polygon needs 3 to 8 finite convex hull vertices");
                return BodyId2D::Invalid;
            }
            std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> vertices{};
            std::ranges::transform(shape.vertices, vertices.begin(), ToNative);
            const auto hull = b2ComputeHull(vertices.data(), static_cast<int>(shape.vertices.size()));
            if (hull.count < 3) {
                Log::GetEngineLogger().Error("Physics2D: degenerate polygon");
                return BodyId2D::Invalid;
            }
        }
    }
    auto body = std::make_unique<Impl::Body>();
    body->id = static_cast<BodyId2D>(m_impl->nextId++);
    body->layer = desc.layer;
    auto def = b2DefaultBodyDef();
    def.type = desc.motion == BodyMotion2D::Static      ? b2_staticBody
               : desc.motion == BodyMotion2D::Kinematic ? b2_kinematicBody
                                                        : b2_dynamicBody;
    def.position = ToNative(desc.position);
    def.rotation = b2MakeRot(desc.angle);
    def.linearVelocity = ToNative(desc.linearVelocity);
    def.angularVelocity = desc.angularVelocity;
    def.gravityScale = desc.gravityScale;
    def.linearDamping = desc.linearDamping;
    def.angularDamping = desc.angularDamping;
    def.fixedRotation = desc.lockRotation;
    def.isBullet = desc.continuous;
    def.enableSleep = desc.allowSleeping;
    def.userData = body.get();
    body->native = b2CreateBody(m_impl->world, &def);
    for (const auto& shape : desc.shapes) {
        auto shapeDef = b2DefaultShapeDef();
        shapeDef.density = 1.0f;
        shapeDef.material.friction = shape.friction;
        shapeDef.material.restitution = shape.restitution;
        shapeDef.isSensor = shape.trigger;
        shapeDef.enableSensorEvents = true;
        shapeDef.filter.categoryBits = std::uint64_t{1} << desc.layer;
        shapeDef.filter.maskBits = m_impl->masks[static_cast<std::size_t>(desc.layer)];
        b2ShapeId native{};
        if (shape.kind == ShapeKind2D::Circle) {
            b2Circle circle{ToNative(shape.center), shape.radius};
            native = b2CreateCircleShape(body->native, &shapeDef, &circle);
        } else if (shape.kind == ShapeKind2D::Capsule) {
            const float half = std::max(0.0f, shape.height * 0.5f - shape.radius);
            if (half <= 0.0f) {
                b2Circle circle{ToNative(shape.center), shape.radius};
                native = b2CreateCircleShape(body->native, &shapeDef, &circle);
            } else {
                b2Capsule capsule{ToNative(shape.center + Vector2{0.0f, -half}),
                                  ToNative(shape.center + Vector2{0.0f, half}), shape.radius};
                native = b2CreateCapsuleShape(body->native, &shapeDef, &capsule);
            }
        } else {
            b2Polygon polygon;
            if (shape.kind == ShapeKind2D::Box) {
                polygon =
                    b2MakeOffsetBox(shape.size.x * 0.5f, shape.size.y * 0.5f, ToNative(shape.center), b2MakeRot(0.0f));
            } else {
                std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> vertices{};
                std::ranges::transform(shape.vertices, vertices.begin(),
                                       [&](Vector2 v) { return ToNative(v + shape.center); });
                auto hull = b2ComputeHull(vertices.data(), static_cast<int>(shape.vertices.size()));
                polygon = b2MakePolygon(&hull, 0.0f);
            }
            native = b2CreatePolygonShape(body->native, &shapeDef, &polygon);
        }
        body->shapes.push_back(native);
    }
    if (desc.motion == BodyMotion2D::Dynamic) {
        auto mass = b2Body_GetMassData(body->native);
        const float ratio = mass.mass > 0.0f ? desc.mass / mass.mass : 1.0f;
        mass.mass = desc.mass;
        mass.rotationalInertia *= ratio;
        b2Body_SetMassData(body->native, mass);
    }
    const auto id = body->id;
    m_impl->bodies.emplace(id, std::move(body));
    return id;
}
void Physics2D::DestroyBody(BodyId2D id) {
    if (auto* body = m_impl->Find(id)) {
        b2DestroyBody(body->native);
        m_impl->bodies.erase(id);
    }
}
void Physics2D::Step(float dt) {
    SPOT_PROFILE_SCOPE("Physics2D.Step");
    if (std::isfinite(dt) && dt > 0.0f)
        b2World_Step(m_impl->world, dt, 4);
}
Pose2D Physics2D::GetPose(BodyId2D id) const {
    if (auto* body = m_impl->Find(id))
        return {FromNative(b2Body_GetPosition(body->native)), b2Rot_GetAngle(b2Body_GetRotation(body->native))};
    return {};
}
void Physics2D::SetPose(BodyId2D id, Pose2D pose) {
    if (auto* body = m_impl->Find(id); body && Finite(pose.position) && std::isfinite(pose.angle)) {
        b2Body_SetTransform(body->native, ToNative(pose.position), b2MakeRot(pose.angle));
        b2Body_SetAwake(body->native, true);
    }
}
void Physics2D::MoveKinematic(BodyId2D id, Pose2D pose, float dt) {
    if (dt <= 0.0f || !std::isfinite(dt))
        return;
    auto current = GetPose(id);
    SetLinearVelocity(id, (pose.position - current.position) / dt);
    SetAngularVelocity(id, std::remainder(pose.angle - current.angle, 6.2831853f) / dt);
}
Vector2 Physics2D::GetLinearVelocity(BodyId2D id) const {
    if (auto* body = m_impl->Find(id))
        return FromNative(b2Body_GetLinearVelocity(body->native));
    return {};
}
void Physics2D::SetLinearVelocity(BodyId2D id, Vector2 v) {
    if (auto* body = m_impl->Find(id); body && Finite(v))
        b2Body_SetLinearVelocity(body->native, ToNative(v));
}
float Physics2D::GetAngularVelocity(BodyId2D id) const {
    if (auto* body = m_impl->Find(id))
        return b2Body_GetAngularVelocity(body->native);
    return 0.0f;
}
void Physics2D::SetAngularVelocity(BodyId2D id, float v) {
    if (auto* body = m_impl->Find(id); body && std::isfinite(v))
        b2Body_SetAngularVelocity(body->native, v);
}
void Physics2D::AddForce(BodyId2D id, Vector2 force) {
    if (auto* body = m_impl->Find(id); body && Finite(force))
        b2Body_ApplyForceToCenter(body->native, ToNative(force), true);
}
void Physics2D::AddImpulse(BodyId2D id, Vector2 impulse) {
    if (auto* body = m_impl->Find(id); body && Finite(impulse))
        b2Body_ApplyLinearImpulseToCenter(body->native, ToNative(impulse), true);
}
void Physics2D::AddForceAtPosition(BodyId2D id, Vector2 force, Vector2 position) {
    if (auto* body = m_impl->Find(id); body && Finite(force) && Finite(position))
        b2Body_ApplyForce(body->native, ToNative(force), ToNative(position), true);
}
void Physics2D::AddImpulseAtPosition(BodyId2D id, Vector2 impulse, Vector2 position) {
    if (auto* body = m_impl->Find(id); body && Finite(impulse) && Finite(position))
        b2Body_ApplyLinearImpulse(body->native, ToNative(impulse), ToNative(position), true);
}
void Physics2D::AddTorque(BodyId2D id, float torque) {
    if (auto* body = m_impl->Find(id); body && std::isfinite(torque))
        b2Body_ApplyTorque(body->native, torque, true);
}
bool Physics2D::IsAwake(BodyId2D id) const {
    if (auto* body = m_impl->Find(id))
        return b2Body_IsAwake(body->native);
    return false;
}
void Physics2D::WakeUp(BodyId2D id) {
    if (auto* body = m_impl->Find(id))
        b2Body_SetAwake(body->native, true);
}
void Physics2D::Sleep(BodyId2D id) {
    if (auto* body = m_impl->Find(id))
        b2Body_SetAwake(body->native, false);
}

void Physics2D::GetContacts(std::vector<Contact2D>& contacts) {
    contacts.clear();
    for (auto& [id, body] : m_impl->bodies) {
        auto& buffer = m_impl->contactBuffer;
        buffer.resize(static_cast<std::size_t>(b2Body_GetContactCapacity(body->native)));
        const int count = b2Body_GetContactData(body->native, buffer.data(), static_cast<int>(buffer.size()));
        for (int i = 0; i < count; ++i) {
            const auto& data = buffer[static_cast<std::size_t>(i)];
            const auto& a = Impl::Owner(data.shapeIdA);
            const auto& b = Impl::Owner(data.shapeIdB);
            if (a.id != id || data.manifold.pointCount == 0)
                continue;
            Contact2D contact{.first = a.id,
                              .second = b.id,
                              .normal = FromNative(data.manifold.normal),
                              .active = b2Body_IsAwake(a.native) || b2Body_IsAwake(b.native)};
            for (int j = 0; j < data.manifold.pointCount; ++j) {
                const auto& point = data.manifold.points[j];
                if (point.separation > 0.01f)
                    continue;
                contact.points[contact.pointCount++] = FromNative(point.point);
                contact.impulse += point.totalNormalImpulse;
            }
            if (contact.pointCount == 0)
                continue;
            auto point = ToNative(contact.points[0]);
            contact.relativeVelocity = FromNative(b2Body_GetWorldPointVelocity(b.native, point)) -
                                       FromNative(b2Body_GetWorldPointVelocity(a.native, point));
            contacts.push_back(contact);
        }
        for (auto shape : body->shapes) {
            if (!b2Shape_IsSensor(shape))
                continue;
            auto& overlaps = m_impl->overlapBuffer;
            overlaps.resize(static_cast<std::size_t>(b2Shape_GetSensorCapacity(shape)));
            const int overlapCount =
                b2Shape_GetSensorOverlaps(shape, overlaps.data(), static_cast<int>(overlaps.size()));
            for (int i = 0; i < overlapCount; ++i) {
                auto other = overlaps[static_cast<std::size_t>(i)];
                if (b2Shape_IsSensor(other))
                    continue;
                contacts.push_back(
                    {.first = id, .second = Impl::Owner(other).id, .firstTrigger = true, .active = true});
            }
        }
    }
}

void Physics2D::RaycastAll(Vector2 origin, Vector2 direction, float distance, const QueryFilter2D& filter,
                           std::vector<QueryHit2D>& hits) const {
    hits.clear();
    if (!Finite(origin) || !Finite(direction) || !std::isfinite(distance) || distance <= 0.0f ||
        Length(direction) == 0.0f)
        return;
    Impl::Query query{filter, hits, distance};
    b2World_CastRay(m_impl->world, ToNative(origin), ToNative(Normalize(direction) * distance), b2DefaultQueryFilter(),
                    Impl::Query::Cast, &query);
    std::ranges::sort(hits, {}, &QueryHit2D::distance);
}
void Physics2D::CircleCast(Vector2 origin, float radius, Vector2 direction, float distance, const QueryFilter2D& filter,
                           std::vector<QueryHit2D>& hits) const {
    OverlapCircle(origin, radius, filter, hits);
    if (!Finite(direction) || !std::isfinite(distance) || distance <= 0.0f || Length(direction) == 0.0f ||
        !Finite(origin) || !std::isfinite(radius) || radius <= 0.0f)
        return;
    auto center = ToNative(origin);
    auto proxy = b2MakeProxy(&center, 1, radius);
    Impl::Query query{filter, hits, distance};
    b2World_CastShape(m_impl->world, &proxy, ToNative(Normalize(direction) * distance), b2DefaultQueryFilter(),
                      Impl::Query::Cast, &query);
    std::ranges::sort(hits, {}, &QueryHit2D::distance);
    // A shape already overlapping the start must appear only once.
    for (std::size_t i = 0; i < hits.size(); ++i) {
        auto first = hits.begin() + static_cast<std::ptrdiff_t>(i + 1);
        hits.erase(std::remove_if(
                       first, hits.end(),
                       [&](const QueryHit2D& hit) { return hit.body == hits[i].body && hit.shape == hits[i].shape; }),
                   hits.end());
    }
}
void Physics2D::OverlapCircle(Vector2 center, float radius, const QueryFilter2D& filter,
                              std::vector<QueryHit2D>& hits) const {
    hits.clear();
    if (!Finite(center) || !std::isfinite(radius) || radius <= 0.0f)
        return;
    auto point = ToNative(center);
    auto proxy = b2MakeProxy(&point, 1, radius);
    Impl::Query query{filter, hits, 0.0f};
    b2World_OverlapShape(m_impl->world, &proxy, b2DefaultQueryFilter(), Impl::Query::Overlap, &query);
}
void Physics2D::OverlapBox(Vector2 center, Vector2 size, float angle, const QueryFilter2D& filter,
                           std::vector<QueryHit2D>& hits) const {
    hits.clear();
    if (!Finite(center) || !Finite(size) || size.x <= 0.0f || size.y <= 0.0f || !std::isfinite(angle))
        return;
    auto polygon = b2MakeOffsetBox(size.x * 0.5f, size.y * 0.5f, ToNative(center), b2MakeRot(angle));
    auto proxy = b2MakeProxy(polygon.vertices, polygon.count, 0.0f);
    Impl::Query query{filter, hits, 0.0f};
    b2World_OverlapShape(m_impl->world, &proxy, b2DefaultQueryFilter(), Impl::Query::Overlap, &query);
}

} // namespace Spot
