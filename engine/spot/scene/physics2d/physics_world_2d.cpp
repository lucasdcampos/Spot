#include "spot/scene/physics2d/physics_world_2d.h"
#include "spot/core/profiler.h"
#include "spot/scene/transform.h"
#include <algorithm>
#include <cmath>

namespace Spot {
PhysicsWorld2D::PhysicsWorld2D() : SceneSystem(SystemOrder::Physics) {
    m_masks.fill(0xffffffffu);
}
void PhysicsWorld2D::SetGravity(Vector2 gravity) {
    if (!std::isfinite(gravity.x) || !std::isfinite(gravity.y))
        return;
    m_gravity = gravity;
    m_simulation.SetGravity(gravity);
}
bool PhysicsWorld2D::GetLayerCollision(int a, int b) const {
    return a >= 0 && a < 32 && b >= 0 && b < 32 && (m_masks[static_cast<std::size_t>(a)] & (1u << b)) != 0;
}
std::uint32_t PhysicsWorld2D::GetLayerMask(int layer) const {
    return layer >= 0 && layer < 32 ? m_masks[static_cast<std::size_t>(layer)] : 0u;
}
void PhysicsWorld2D::SetLayerCollision(int a, int b, bool collides) {
    if (a < 0 || a >= 32 || b < 0 || b >= 32)
        return;
    for (auto [layer, other] : {std::pair{a, b}, std::pair{b, a}}) {
        auto& mask = m_masks[static_cast<std::size_t>(layer)];
        mask = collides ? mask | (1u << other) : mask & ~(1u << other);
    }
    m_simulation.SetLayerMasks(m_masks);
}
void PhysicsWorld2D::SetLayerMask(int layer, std::uint32_t mask) {
    if (layer < 0 || layer >= 32)
        return;
    for (int other = 0; other < 32; ++other)
        SetLayerCollision(layer, other, (mask & (1u << other)) != 0);
}
void PhysicsWorld2D::Synchronize() {
    for (auto& [entity, body] : m_bodies)
        body.seen = false;
    GetScene().ForEachEntity([&](Entity& entity) {
        if (!entity.IsActiveInHierarchy())
            return;
        auto* rigidbody = entity.GetComponent<Rigidbody2D>();
        if (rigidbody && !rigidbody->IsActiveAndEnabled())
            rigidbody = nullptr;
        std::size_t stamp = static_cast<std::size_t>(entity.GetLayer());
        const auto addStamp = [&](const Component& component, std::uint64_t revision) {
            stamp ^= std::hash<ComponentId>{}(component.GetId()) + static_cast<std::size_t>(revision) + 0x9e3779b9u +
                     (stamp << 6u) + (stamp >> 2u);
        };
        if (rigidbody)
            addStamp(*rigidbody, rigidbody->m_revision);
        bool hasCollider = false;
        entity.ForEachComponent<Collider2D>([&](Collider2D& collider) {
            if (collider.IsActiveAndEnabled()) {
                hasCollider = true;
                addStamp(collider, collider.GetRevision());
            }
        });
        if (!rigidbody && !hasCollider)
            return;
        auto& transform = entity.GetTransform();
        const auto angle = transform.GetWorldRotation2D();
        const auto& matrix = transform.GetWorldMatrix();
        const Vector2 right{std::cos(angle), std::sin(angle)}, up{-right.y, right.x};
        const Vector2 scale{Dot(Vector2{matrix.columns[0].x, matrix.columns[0].y}, right),
                            Dot(Vector2{matrix.columns[1].x, matrix.columns[1].y}, up)};
        auto [found, added] = m_bodies.try_emplace(entity.GetId());
        auto& body = found->second;
        body.seen = true;
        if (!added && body.stamp == stamp && body.scale == scale)
            return;
        BodyDesc2D desc = rigidbody ? rigidbody->m_desc : BodyDesc2D{};
        const auto position = transform.GetWorldPosition();
        desc.position = {position.x, position.y};
        desc.angle = transform.GetWorldRotation2D();
        desc.layer = entity.GetLayer();
        if (body.id != BodyId2D::Invalid) {
            desc.linearVelocity = m_simulation.GetLinearVelocity(body.id);
            desc.angularVelocity = m_simulation.GetAngularVelocity(body.id);
            m_owners.erase(body.id);
            m_simulation.DestroyBody(body.id);
        }
        body.colliders.clear();
        entity.ForEachComponent<Collider2D>([&](Collider2D& collider) {
            if (collider.IsActiveAndEnabled()) {
                const auto before = desc.shapes.size();
                collider.AppendShapes(scale, desc.shapes);
                body.colliders.insert(body.colliders.end(), desc.shapes.size() - before, collider.GetId());
            }
        });
        body.id = m_simulation.CreateBody(desc);
        body.stamp = stamp;
        body.scale = scale;
        body.pose = {desc.position, desc.angle};
        body.motion = desc.motion;
        if (body.id != BodyId2D::Invalid)
            m_owners.emplace(body.id, entity.GetId());
    });
    for (auto it = m_bodies.begin(); it != m_bodies.end();) {
        if (!it->second.seen) {
            m_simulation.DestroyBody(it->second.id);
            m_owners.erase(it->second.id);
            it = m_bodies.erase(it);
        } else
            ++it;
    }
}
BodyId2D PhysicsWorld2D::FindBody(EntityId entity) {
    Synchronize();
    auto found = m_bodies.find(entity);
    return found == m_bodies.end() ? BodyId2D::Invalid : found->second.id;
}
void PhysicsWorld2D::OnFixedUpdate(float dt) {
    SPOT_PROFILE_SCOPE("PhysicsWorld2D");
    Synchronize();
    for (auto& [id, body] : m_bodies) {
        auto* entity = GetScene().GetEntity(id);
        const auto& transform = entity->GetTransform();
        const auto position = transform.GetWorldPosition();
        const Pose2D pose{{position.x, position.y}, transform.GetWorldRotation2D()};
        if (body.motion == BodyMotion2D::Kinematic)
            m_simulation.MoveKinematic(body.id, pose, dt);
        else if (pose.position != body.pose.position || pose.angle != body.pose.angle)
            m_simulation.SetPose(body.id, pose);
    }
    m_simulation.Step(dt);
    for (auto& [id, body] : m_bodies) {
        body.pose = m_simulation.GetPose(body.id);
        if (body.id == BodyId2D::Invalid || body.motion != BodyMotion2D::Dynamic)
            continue;
        auto& transform = GetScene().GetEntity(id)->GetTransform();
        transform.SetWorldPosition({body.pose.position.x, body.pose.position.y, transform.GetWorldPosition().z});
        transform.SetWorldRotation(Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, body.pose.angle));
    }
    DispatchContacts();
}
QueryFilter2D PhysicsWorld2D::MakeFilter(const QueryFilter& filter) {
    Synchronize();
    auto found = m_bodies.find(filter.ignore);
    return {filter.layerMask, filter.includeTriggers, found == m_bodies.end() ? BodyId2D::Invalid : found->second.id};
}
std::vector<RaycastHit2D> PhysicsWorld2D::ConvertHits() {
    std::vector<RaycastHit2D> result;
    result.reserve(m_hits.size());
    for (const auto& hit : m_hits) {
        auto owner = m_owners.find(hit.body);
        if (owner == m_owners.end())
            continue;
        auto* entity = GetScene().GetEntity(owner->second);
        auto& body = m_bodies.at(owner->second);
        Collider2D* collider = nullptr;
        if (hit.shape < body.colliders.size()) {
            entity->ForEachComponent<Collider2D>([&](Collider2D& candidate) {
                if (candidate.GetId() == body.colliders[hit.shape])
                    collider = &candidate;
            });
        }
        result.push_back({owner->second, collider, hit.point, hit.normal, hit.distance});
    }
    return result;
}
std::vector<RaycastHit2D> PhysicsWorld2D::RaycastAll(Vector2 o, Vector2 d, float distance, const QueryFilter& filter) {
    const auto native = MakeFilter(filter);
    m_simulation.RaycastAll(o, d, distance, native, m_hits);
    return ConvertHits();
}
std::optional<RaycastHit2D> PhysicsWorld2D::Raycast(Vector2 o, Vector2 d, float distance, const QueryFilter& filter) {
    auto hits = RaycastAll(o, d, distance, filter);
    return hits.empty() ? std::nullopt : std::optional{hits.front()};
}
std::optional<RaycastHit2D> PhysicsWorld2D::CircleCast(Vector2 o, float radius, Vector2 d, float distance,
                                                       const QueryFilter& filter) {
    const auto native = MakeFilter(filter);
    m_simulation.CircleCast(o, radius, d, distance, native, m_hits);
    auto hits = ConvertHits();
    return hits.empty() ? std::nullopt : std::optional{hits.front()};
}
std::vector<RaycastHit2D> PhysicsWorld2D::OverlapCircle(Vector2 center, float radius, const QueryFilter& filter) {
    const auto native = MakeFilter(filter);
    m_simulation.OverlapCircle(center, radius, native, m_hits);
    return ConvertHits();
}
std::vector<RaycastHit2D> PhysicsWorld2D::OverlapBox(Vector2 center, Vector2 size, float angle,
                                                     const QueryFilter& filter) {
    const auto native = MakeFilter(filter);
    m_simulation.OverlapBox(center, size, angle, native, m_hits);
    return ConvertHits();
}
void PhysicsWorld2D::Dispatch(const PairContact& contact, int phase) {
    for (bool first : {true, false}) {
        const auto id = first ? contact.pair.first : contact.pair.second;
        if (auto* entity = GetScene().GetEntity(id)) {
            Collision2D collision = contact.collision;
            collision.other = first ? contact.pair.second : contact.pair.first;
            if (!first) {
                collision.normal = -collision.normal;
                collision.relativeVelocity = -collision.relativeVelocity;
            }
            const TriggerContact2D trigger{collision.other, first == contact.pair.firstTrigger};
            entity->ForEachComponent<Component>([&](Component& component) {
                if (!component.IsActiveAndEnabled())
                    return;
                if (contact.pair.trigger) {
                    if (phase == 0)
                        component.InvokeHook("OnTriggerEnter2D", [&] { component.OnTriggerEnter2D(trigger); });
                    if (phase == 2)
                        component.InvokeHook("OnTriggerExit2D", [&] { component.OnTriggerExit2D(trigger); });
                } else {
                    if (phase == 0)
                        component.InvokeHook("OnCollisionEnter2D", [&] { component.OnCollisionEnter2D(collision); });
                    if (phase == 1 && contact.active)
                        component.InvokeHook("OnCollisionStay2D", [&] { component.OnCollisionStay2D(collision); });
                    if (phase == 2)
                        component.InvokeHook("OnCollisionExit2D", [&] { component.OnCollisionExit2D(collision); });
                }
            });
        }
    }
}
void PhysicsWorld2D::DispatchContacts() {
    m_simulation.GetContacts(m_contacts);
    m_current.clear();
    for (const auto& contact : m_contacts) {
        const auto a = m_owners.find(contact.first), b = m_owners.find(contact.second);
        if (a == m_owners.end() || b == m_owners.end() || a->second == b->second)
            continue;
        const bool swap = b->second < a->second;
        Pair pair{swap ? b->second : a->second, swap ? a->second : b->second,
                  contact.firstTrigger || contact.secondTrigger, swap ? contact.secondTrigger : contact.firstTrigger};
        Collision2D collision{.other = pair.second,
                              .points = contact.points,
                              .pointCount = contact.pointCount,
                              .normal = swap ? contact.normal : -contact.normal,
                              .relativeVelocity = swap ? contact.relativeVelocity : -contact.relativeVelocity,
                              .impulse = contact.impulse};
        m_current.push_back({pair, collision, contact.active});
    }
    std::ranges::sort(m_current, [](const PairContact& a, const PairContact& b) {
        return a.pair != b.pair ? a.pair < b.pair : a.collision.impulse > b.collision.impulse;
    });
    std::size_t kept = 0;
    for (std::size_t i = 0; i < m_current.size(); ++i) {
        if (kept > 0 && m_current[kept - 1].pair == m_current[i].pair)
            m_current[kept - 1].active |= m_current[i].active;
        else
            m_current[kept++] = m_current[i];
    }
    m_current.resize(kept);
    // Finish the snapshot before callbacks can query, edit or destroy the scene.
    std::size_t old = 0, current = 0;
    while (old < m_previous.size() || current < m_current.size()) {
        if (old == m_previous.size() || (current < m_current.size() && m_current[current].pair < m_previous[old].pair))
            Dispatch(m_current[current++], 0);
        else if (current == m_current.size() || m_previous[old].pair < m_current[current].pair)
            Dispatch(m_previous[old++], 2);
        else {
            Dispatch(m_current[current++], 1);
            ++old;
        }
    }
    m_previous.swap(m_current);
}
} // namespace Spot
