#include "scene/physics/physics_world.h"

#include "core/assert.h"
#include "scene/physics/character_controller.h"
#include "scene/physics/collider.h"
#include "scene/physics/rigidbody.h"
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
        CreatePendingJoints();
        GetSimulation().Step(fixedDeltaTime);
        FollowBodies();
        BreakJoints();
    }
    // Also without bodies, as the last ones going away ends their contacts.
    DispatchContacts();
}

void PhysicsWorld::ForgetBody(PhysicsBodyId body) {
    m_simulation->DestroyBody(body);
    m_owners.erase(body);
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
        // Joints cannot outlive the bodies they hold.
        ReleaseJoints(entityId);
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
