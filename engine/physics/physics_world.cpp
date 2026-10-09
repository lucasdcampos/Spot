#include "physics/physics_world.h"

#include "core/assert.h"
#include "physics/character_controller.h"
#include "physics/collider.h"
#include "physics/rigidbody.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Spot {

using Detail::PhysicsBodyId;
using Detail::PhysicsMotion;

namespace {

Vector3 GetWorldScale(const Transform& transform) {
    const Matrix4& matrix = transform.GetWorldMatrix();
    const auto axisLength = [&matrix](int axis) {
        const Vector4& column = matrix.columns[static_cast<std::size_t>(axis)];
        return Length(Vector3{column.x, column.y, column.z});
    };
    return {axisLength(0), axisLength(1), axisLength(2)};
}

// The scale read back from a world matrix drifts by rounding as the entity turns, which must not rebuild its body.
bool IsSameScale(Vector3 a, Vector3 b) {
    constexpr float Tolerance = 1e-4f;
    const auto same = [](float x, float y) { return std::abs(x - y) <= Tolerance * std::max(std::abs(y), 1.0f); };
    return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}

} // namespace

PhysicsWorld::PhysicsWorld() : SceneSystem(SystemOrder::Physics) {
    m_layerMasks.fill(0xffffffffu);
    m_layerNames[0] = "Default";
}

bool PhysicsWorld::GetLayerCollision(int a, int b) const {
    return ((GetLayerMask(a) >> b) & 1u) != 0;
}

void PhysicsWorld::SetLayerCollision(int a, int b, bool collides) {
    std::uint32_t mask = GetLayerMask(a);
    mask = collides ? mask | (1u << b) : mask & ~(1u << b);
    SetLayerMask(a, mask);
}

void PhysicsWorld::SetLayerMask(int layer, std::uint32_t mask) {
    SPOT_ASSERT(layer >= 0 && layer < Entity::LayerCount, "Layers go from 0 to Entity::LayerCount - 1");
    for (int other = 0; other < Entity::LayerCount; ++other) {
        const std::uint32_t bit = 1u << layer;
        std::uint32_t& otherMask = m_layerMasks[static_cast<std::size_t>(other)];
        otherMask = ((mask >> other) & 1u) != 0 ? otherMask | bit : otherMask & ~bit;
    }
    m_layerMasks[static_cast<std::size_t>(layer)] = mask;
    if (m_simulation) {
        m_simulation->SetLayerMasks(m_layerMasks);
    }
}

void PhysicsWorld::SetLayerNames(std::span<const std::string> names) {
    for (std::size_t i = 0; i < m_layerNames.size(); ++i) {
        m_layerNames[i] = i < names.size() ? names[i] : std::string();
    }
}

std::optional<int> PhysicsWorld::FindLayer(std::string_view name) const {
    const auto found = std::ranges::find(m_layerNames, name);
    if (name.empty() || found == m_layerNames.end()) {
        return std::nullopt;
    }
    return static_cast<int>(found - m_layerNames.begin());
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::SetGravity(Vector3 gravity) {
    m_gravity = gravity;
    if (m_simulation) {
        m_simulation->SetGravity(gravity);
    }
}

void PhysicsWorld::OnFixedUpdate(float fixedDeltaTime) {
    m_stepTime = fixedDeltaTime;
    RebuildChanged();
    if (!m_bodies.empty()) {
        FollowEntities();
        GetSimulation().Step(fixedDeltaTime);
        FollowBodies();
    }
    // Also without bodies, as the last ones going away ends their contacts.
    DispatchContacts();
}

Detail::PhysicsQueryFilter PhysicsWorld::MakeQueryFilter(const QueryFilter& filter) const {
    Detail::PhysicsQueryFilter query{.layerMask = filter.layerMask, .sensors = filter.includeTriggers};
    if (const auto ignored = m_bodies.find(filter.ignore); ignored != m_bodies.end()) {
        query.ignored = {ignored->second.id, ignored->second.sensor};
    }
    return query;
}

bool PhysicsWorld::PrepareQuery() {
    RebuildChanged();
    return m_simulation && !m_bodies.empty();
}

RaycastHit PhysicsWorld::MakeHit(const Detail::PhysicsQueryHit& hit) const {
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
    for (const Detail::PhysicsQueryHit& hit : m_queryHits) {
        const RaycastHit found = MakeHit(hit);
        hits.push_back({found.entity, found.collider});
    }
    return hits;
}

std::optional<RaycastHit> PhysicsWorld::Raycast(Vector3 origin, Vector3 direction, float maxDistance,
                                                const QueryFilter& filter) {
    Detail::PhysicsQueryHit hit;
    if (!PrepareQuery() || LengthSquared(direction) <= 0.0f || maxDistance <= 0.0f ||
        !m_simulation->CastRay(origin, Normalize(direction), maxDistance, MakeQueryFilter(filter), hit)) {
        return std::nullopt;
    }
    return MakeHit(hit);
}

std::vector<RaycastHit> PhysicsWorld::RaycastAll(Vector3 origin, Vector3 direction, float maxDistance,
                                                 const QueryFilter& filter) {
    std::vector<RaycastHit> hits;
    if (!PrepareQuery() || LengthSquared(direction) <= 0.0f || maxDistance <= 0.0f) {
        return hits;
    }
    m_simulation->CastRayAll(origin, Normalize(direction), maxDistance, MakeQueryFilter(filter), m_queryHits);
    for (const Detail::PhysicsQueryHit& hit : m_queryHits) {
        hits.push_back(MakeHit(hit));
    }
    return hits;
}

std::optional<RaycastHit> PhysicsWorld::SphereCast(Vector3 origin, float radius, Vector3 direction, float maxDistance,
                                                   const QueryFilter& filter) {
    Detail::PhysicsQueryHit hit;
    if (!PrepareQuery() || LengthSquared(direction) <= 0.0f || maxDistance < 0.0f ||
        !m_simulation->CastSphere(origin, radius, Normalize(direction), maxDistance, MakeQueryFilter(filter), hit)) {
        return std::nullopt;
    }
    return MakeHit(hit);
}

std::vector<OverlapHit> PhysicsWorld::OverlapSphere(Vector3 center, float radius, const QueryFilter& filter) {
    if (!PrepareQuery()) {
        return {};
    }
    m_simulation->OverlapSphere(center, radius, MakeQueryFilter(filter), m_queryHits);
    return MakeOverlapHits();
}

std::vector<OverlapHit> PhysicsWorld::OverlapBox(Vector3 center, Vector3 size, const Quaternion& rotation,
                                                 const QueryFilter& filter) {
    if (!PrepareQuery()) {
        return {};
    }
    m_simulation->OverlapBox(center, size * 0.5f, Normalize(rotation), MakeQueryFilter(filter), m_queryHits);
    return MakeOverlapHits();
}

void PhysicsWorld::ForgetBody(PhysicsBodyId body) {
    m_simulation->DestroyBody(body);
    m_owners.erase(body);
}

namespace {

// The contact as the first or second entity sees it: the normal points toward it and the velocity is its own minus
// the other's.
Collision MakeCollision(const Detail::PhysicsContact& contact, EntityId other, bool forFirst) {
    Collision collision{
        .other = other, .points = contact.points, .pointCount = contact.pointCount, .impulse = contact.impulse};
    collision.normal = forFirst ? -contact.normal : contact.normal;
    collision.relativeVelocity = forFirst ? -contact.relativeVelocity : contact.relativeVelocity;
    return collision;
}

Collision Flip(const Collision& collision, EntityId other) {
    Collision flipped = collision;
    flipped.other = other;
    flipped.normal = -collision.normal;
    flipped.relativeVelocity = -collision.relativeVelocity;
    return flipped;
}

} // namespace

PhysicsWorld::Body* PhysicsWorld::FindCharacter(EntityId entity) {
    if (m_changed.erase(entity) > 0) {
        Rebuild(entity);
    }
    const auto found = m_bodies.find(entity);
    return found != m_bodies.end() && found->second.character != Detail::PhysicsCharacterId::Invalid ? &found->second
                                                                                                     : nullptr;
}

void PhysicsWorld::MoveCharacter(EntityId entityId, Vector3 velocity, float deltaTime) {
    Body* body = FindCharacter(entityId);
    Entity* entity = GetScene().GetEntity(entityId);
    if (!body || !entity) {
        return;
    }
    Transform& transform = entity->GetTransform();
    // Code may have placed the entity elsewhere since the last move.
    if (transform.GetWorldPosition() != body->position) {
        m_simulation->SetCharacterPosition(body->character, transform.GetWorldPosition() + body->characterCenter);
    }
    const auto* controller = entity->GetComponent<CharacterController>();
    m_simulation->MoveCharacter(body->character, velocity, deltaTime, controller ? controller->m_stepHeight : 0.0f,
                                body->characterContacts);
    body->characterMoved = true;
    transform.SetWorldPosition(m_simulation->GetCharacterState(body->character).position - body->characterCenter);
    body->position = transform.GetWorldPosition();
}

std::optional<Detail::PhysicsCharacterState> PhysicsWorld::GetCharacterState(EntityId entity) {
    const Body* body = FindCharacter(entity);
    if (!body) {
        return std::nullopt;
    }
    return m_simulation->GetCharacterState(body->character);
}

EntityId PhysicsWorld::GetCharacterGround(EntityId entity) {
    const auto state = GetCharacterState(entity);
    if (!state) {
        return EntityId::Invalid;
    }
    const auto owner = m_owners.find(state->groundBody);
    return owner != m_owners.end() ? owner->second.entity : EntityId::Invalid;
}

void PhysicsWorld::AddCharacterContacts() {
    for (auto& [entity, body] : m_bodies) {
        if (body.character == Detail::PhysicsCharacterId::Invalid) {
            continue;
        }
        const Vector3 velocity = m_simulation->GetCharacterState(body.character).velocity;
        for (const Detail::PhysicsCharacterContact& contact : body.characterContacts) {
            const auto owner = m_owners.find(contact.body);
            if (owner == m_owners.end() || owner->second.entity == entity) {
                continue;
            }
            const EntityId other = owner->second.entity;
            const bool trigger = contact.sensor || owner->second.sensor;
            // As the character sees it.
            Collision collision{.other = other, .pointCount = 1, .normal = contact.normal};
            collision.points[0] = contact.point;
            collision.relativeVelocity = velocity - contact.velocity;
            collision.impulse = std::max(-Dot(collision.relativeVelocity, contact.normal), 0.0f) * body.characterMass;
            const bool characterFirst = entity < other;
            m_current.push_back({.pair = {characterFirst ? entity : other, characterFirst ? other : entity, trigger,
                                          trigger && !characterFirst},
                                 .collision = characterFirst ? collision : Flip(collision, entity),
                                 .active = body.characterMoved});
        }
        body.characterMoved = false;
    }
}

void PhysicsWorld::DispatchContacts() {
    // The pairs touching now, from every contact between their bodies, sorted so the same pairs come together; the
    // hardest hit describes a pair. The vectors keep their memory from step to step.
    m_current.clear();
    m_contactBuffer.clear();
    if (m_simulation) {
        m_simulation->GetContacts(m_contactBuffer);
    }
    for (const Detail::PhysicsContact& contact : m_contactBuffer) {
        const auto first = m_owners.find(contact.first);
        const auto second = m_owners.find(contact.second);
        if (first == m_owners.end() || second == m_owners.end() || first->second.entity == second->second.entity ||
            (first->second.sensor && second->second.sensor)) {
            continue;
        }
        const bool swap = second->second.entity < first->second.entity;
        const Owner& low = swap ? second->second : first->second;
        const Owner& high = swap ? first->second : second->second;
        const bool trigger = low.sensor || high.sensor;
        // As the lower entity sees it, which is the contact's first body unless swapped.
        m_current.push_back({.pair = {low.entity, high.entity, trigger, trigger && low.sensor},
                             .collision = MakeCollision(contact, high.entity, !swap),
                             .active = contact.active});
    }
    AddCharacterContacts();
    std::ranges::sort(m_current, [](const PairContact& a, const PairContact& b) {
        return a.pair != b.pair ? a.pair < b.pair : a.collision.impulse > b.collision.impulse;
    });
    // Each pair once, keeping its hardest hit, and active when any of its contacts is.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < m_current.size(); ++i) {
        if (kept > 0 && m_current[kept - 1].pair == m_current[i].pair) {
            m_current[kept - 1].active = m_current[kept - 1].active || m_current[i].active;
        } else {
            m_current[kept++] = m_current[i];
        }
    }
    m_current.resize(kept);

    // Both lists are sorted, so a walk through both finds the pairs that ended, began and went on. The events go out
    // once the lists are known, so hooks that change bodies affect the next step only.
    std::swap(m_current, m_previous);
    const std::vector<PairContact>& now = m_previous;
    const std::vector<PairContact>& before = m_current;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < before.size() || j < now.size()) {
        if (j == now.size() || (i < before.size() && before[i].pair < now[j].pair)) {
            SendContact(before[i++], ContactPhase::Exit);
        } else if (i == before.size() || now[j].pair < before[i].pair) {
            SendContact(now[j++], ContactPhase::Enter);
        } else {
            if (now[j].active) {
                SendContact(now[j], ContactPhase::Stay);
            }
            ++i;
            ++j;
        }
    }
}

void PhysicsWorld::SendContact(const PairContact& contact, ContactPhase phase) {
    const auto notify = [this](EntityId id, const char* hook, const auto& call) {
        Entity* entity = GetScene().GetEntity(id);
        if (!entity) {
            return;
        }
        entity->ForEachComponent<Component>([&](Component& component) {
            if (component.IsActiveAndEnabled()) {
                component.InvokeHook(hook, [&] { call(component); });
            }
        });
    };
    const ContactPair& pair = contact.pair;
    if (pair.trigger) {
        const TriggerContact forFirst{.other = pair.second, .isTrigger = pair.firstIsTrigger};
        const TriggerContact forSecond{.other = pair.first, .isTrigger = !pair.firstIsTrigger};
        if (phase == ContactPhase::Enter) {
            notify(pair.first, "OnTriggerEnter", [&](Component& c) { c.OnTriggerEnter(forFirst); });
            notify(pair.second, "OnTriggerEnter", [&](Component& c) { c.OnTriggerEnter(forSecond); });
        } else if (phase == ContactPhase::Exit) {
            notify(pair.first, "OnTriggerExit", [&](Component& c) { c.OnTriggerExit(forFirst); });
            notify(pair.second, "OnTriggerExit", [&](Component& c) { c.OnTriggerExit(forSecond); });
        }
        return;
    }
    const auto deliver = [&](EntityId id, const Collision& seen) {
        switch (phase) {
        case ContactPhase::Enter:
            notify(id, "OnCollisionEnter", [&](Component& c) { c.OnCollisionEnter(seen); });
            break;
        case ContactPhase::Stay:
            notify(id, "OnCollisionStay", [&](Component& c) { c.OnCollisionStay(seen); });
            break;
        case ContactPhase::Exit:
            notify(id, "OnCollisionExit", [&](Component& c) { c.OnCollisionExit(seen); });
            break;
        }
    };
    deliver(pair.first, contact.collision);
    deliver(pair.second, Flip(contact.collision, pair.first));
}

void PhysicsWorld::MarkChanged(EntityId entity) {
    m_changed.insert(entity);
}

std::size_t PhysicsWorld::GetBodyCount() const {
    std::size_t count = 0;
    for (const auto& [entity, body] : m_bodies) {
        count += (body.id != PhysicsBodyId::Invalid ? 1 : 0) + (body.sensor != PhysicsBodyId::Invalid ? 1 : 0);
    }
    return count;
}

const PhysicsWorld::Body* PhysicsWorld::FindMovingBody(EntityId entity) {
    if (m_changed.erase(entity) > 0) {
        Rebuild(entity);
    }
    const auto found = m_bodies.find(entity);
    return found != m_bodies.end() && found->second.id != PhysicsBodyId::Invalid &&
                   found->second.motion != PhysicsMotion::Static
               ? &found->second
               : nullptr;
}

Detail::PhysicsSimulation& PhysicsWorld::GetSimulation() {
    if (!m_simulation) {
        m_simulation = Detail::PhysicsSimulation::Create();
        m_simulation->SetGravity(m_gravity);
        m_simulation->SetLayerMasks(m_layerMasks);
    }
    return *m_simulation;
}

void PhysicsWorld::RebuildChanged() {
    // Sorted, so bodies are created in the same order on every run and the simulation stays reproducible.
    std::vector<EntityId> changed(m_changed.begin(), m_changed.end());
    m_changed.clear();
    std::ranges::sort(changed);
    for (const EntityId entity : changed) {
        Rebuild(entity);
    }
}

void PhysicsWorld::Rebuild(EntityId entityId) {
    Vector3 linearVelocity = {};
    Vector3 angularVelocity = {};
    bool keepVelocity = false;
    if (const auto found = m_bodies.find(entityId); found != m_bodies.end()) {
        const Body& old = found->second;
        if (old.id != PhysicsBodyId::Invalid) {
            if (old.motion == PhysicsMotion::Dynamic) {
                linearVelocity = m_simulation->GetLinearVelocity(old.id);
                angularVelocity = m_simulation->GetAngularVelocity(old.id);
                keepVelocity = true;
            }
            ForgetBody(old.id);
        }
        if (old.sensor != PhysicsBodyId::Invalid) {
            ForgetBody(old.sensor);
        }
        if (old.character != Detail::PhysicsCharacterId::Invalid) {
            m_owners.erase(m_simulation->GetCharacterBody(old.character));
            m_simulation->DestroyCharacter(old.character);
        }
        m_bodies.erase(found);
    }

    Entity* entity = GetScene().GetEntity(entityId);
    if (!entity || !entity->IsActiveInHierarchy()) {
        return;
    }

    const Transform& transform = entity->GetTransform();
    const Vector3 scale = GetWorldScale(transform);
    Detail::PhysicsBodyDesc desc;
    Detail::PhysicsBodyDesc sensor{.sensor = true};
    std::vector<Collider*> solidColliders;
    std::vector<Collider*> triggerColliders;
    for (Collider* collider : entity->GetComponents<Collider>()) {
        if (!collider->IsActiveAndEnabled()) {
            continue;
        }
        Detail::PhysicsBodyDesc& target = collider->IsTrigger() ? sensor : desc;
        (collider->IsTrigger() ? triggerColliders : solidColliders).push_back(collider);
        if (target.shapes.empty()) {
            target.friction = collider->GetFriction();
            target.restitution = collider->GetRestitution();
        }
        target.shapes.push_back(collider->GetShape(scale));
    }

    Rigidbody* rigidbody = nullptr;
    for (Rigidbody* candidate : entity->GetComponents<Rigidbody>()) {
        if (candidate->IsActiveAndEnabled()) {
            rigidbody = candidate;
            break;
        }
    }
    const CharacterController* controller = nullptr;
    for (const CharacterController* candidate : entity->GetComponents<CharacterController>()) {
        if (candidate->IsActiveAndEnabled()) {
            controller = candidate;
            break;
        }
    }
    const bool solid = !desc.shapes.empty() || rigidbody;
    if (!solid && sensor.shapes.empty() && !controller) {
        return;
    }

    desc.position = transform.GetWorldPosition();
    desc.rotation = Normalize(transform.GetWorldRotation());
    desc.layer = entity->GetLayer();
    // Dynamic bodies cannot be made of triangles, so their mesh colliders take their convex hull.
    if (rigidbody && !rigidbody->m_kinematic) {
        for (Detail::PhysicsShape& shape : desc.shapes) {
            if (shape.kind == Detail::PhysicsShapeKind::Mesh) {
                shape.kind = Detail::PhysicsShapeKind::ConvexHull;
            }
        }
    }
    if (rigidbody) {
        desc.motion = rigidbody->m_kinematic ? PhysicsMotion::Kinematic : PhysicsMotion::Dynamic;
        desc.mass = rigidbody->m_mass;
        desc.gravityScale = rigidbody->m_gravityScale;
        desc.linearDamping = rigidbody->m_linearDamping;
        desc.angularDamping = rigidbody->m_angularDamping;
        desc.lockRotationX = rigidbody->m_rotationLocks.x;
        desc.lockRotationY = rigidbody->m_rotationLocks.y;
        desc.lockRotationZ = rigidbody->m_rotationLocks.z;
        desc.continuous = rigidbody->m_continuous;
        desc.allowSleeping = rigidbody->m_canSleep;
        desc.linearVelocity = keepVelocity ? linearVelocity : rigidbody->m_linearVelocity;
        desc.angularVelocity = keepVelocity ? angularVelocity : rigidbody->m_angularVelocity;
        rigidbody->m_linearVelocity = {};
        rigidbody->m_angularVelocity = {};
    }

    Body body{
        .motion = desc.motion, .position = desc.position, .rotation = transform.GetWorldRotation(), .scale = scale};
    if (solid) {
        body.id = GetSimulation().CreateBody(desc);
        if (body.id != PhysicsBodyId::Invalid) {
            m_owners.emplace(body.id, Owner{entityId, false, std::move(solidColliders)});
        }
    }
    if (!sensor.shapes.empty()) {
        // Triggers of moving entities follow them as kinematic bodies, so they sweep through what they pass.
        sensor.motion = rigidbody ? PhysicsMotion::Kinematic : PhysicsMotion::Static;
        sensor.position = desc.position;
        sensor.rotation = desc.rotation;
        sensor.layer = desc.layer;
        body.sensor = GetSimulation().CreateBody(sensor);
        if (body.sensor != PhysicsBodyId::Invalid) {
            m_owners.emplace(body.sensor, Owner{entityId, true, std::move(triggerColliders)});
        }
    }
    if (controller) {
        body.character = GetSimulation().CreateCharacter({.position = desc.position + controller->m_center,
                                                          .radius = controller->m_radius,
                                                          .height = controller->m_height,
                                                          .maxSlopeAngle = controller->m_slopeLimit,
                                                          .mass = controller->m_mass,
                                                          .maxStrength = controller->m_pushStrength,
                                                          .layer = desc.layer});
        if (body.character != Detail::PhysicsCharacterId::Invalid) {
            body.characterCenter = controller->m_center;
            body.characterMass = controller->m_mass;
            m_owners.emplace(m_simulation->GetCharacterBody(body.character), Owner{entityId, false, {}});
        }
    }
    if (body.id != PhysicsBodyId::Invalid || body.sensor != PhysicsBodyId::Invalid ||
        body.character != Detail::PhysicsCharacterId::Invalid) {
        m_bodies.emplace(entityId, std::move(body));
    }
}

void PhysicsWorld::FollowEntities() {
    std::vector<EntityId> outdated;
    for (auto& [entityId, body] : m_bodies) {
        const Entity* entity = GetScene().GetEntity(entityId);
        if (!entity || !entity->IsActiveInHierarchy()) {
            outdated.push_back(entityId);
            continue;
        }

        const Transform& transform = entity->GetTransform();
        if (!IsSameScale(GetWorldScale(transform), body.scale)) {
            outdated.push_back(entityId);
            continue;
        }

        const Vector3 position = transform.GetWorldPosition();
        const Quaternion& rotation = transform.GetWorldRotation();
        const bool moved = position != body.position || rotation != body.rotation;
        if (body.id != PhysicsBodyId::Invalid) {
            if (body.motion == PhysicsMotion::Kinematic) {
                // Every step, so the body also stops once its entity does.
                m_simulation->MoveKinematic(body.id, position, Normalize(rotation), m_stepTime);
            } else if (moved) {
                m_simulation->SetPose(body.id, position, Normalize(rotation));
            }
        }
        if (body.character != Detail::PhysicsCharacterId::Invalid && moved) {
            m_simulation->SetCharacterPosition(body.character, position + body.characterCenter);
        }
        if (body.sensor != PhysicsBodyId::Invalid) {
            // Sensors of moving entities are kinematic, and catch up with where their dynamic body took the entity.
            if (body.motion != PhysicsMotion::Static) {
                m_simulation->MoveKinematic(body.sensor, position, Normalize(rotation), m_stepTime);
            } else if (moved) {
                m_simulation->SetPose(body.sensor, position, Normalize(rotation));
            }
        }
        body.position = position;
        body.rotation = rotation;
    }

    std::ranges::sort(outdated);
    for (const EntityId entity : outdated) {
        Rebuild(entity);
    }
}

void PhysicsWorld::FollowBodies() {
    for (auto& [entityId, body] : m_bodies) {
        if (body.id == PhysicsBodyId::Invalid || body.motion != PhysicsMotion::Dynamic ||
            !m_simulation->IsAwake(body.id)) {
            continue;
        }
        Entity* entity = GetScene().GetEntity(entityId);
        if (!entity) {
            continue;
        }
        const Detail::PhysicsPose pose = m_simulation->GetPose(body.id);
        Transform& transform = entity->GetTransform();
        transform.SetWorldPosition(pose.position);
        transform.SetWorldRotation(pose.rotation);
        // Read back rather than taken from the pose, since going through a parent may round them differently.
        body.position = transform.GetWorldPosition();
        body.rotation = transform.GetWorldRotation();
    }
}

} // namespace Spot
