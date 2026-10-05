#include "physics/physics_world.h"

#include "core/assert.h"
#include "physics/collider.h"
#include "physics/rigidbody.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>
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

} // namespace

PhysicsWorld::PhysicsWorld(Scene& scene) : m_scene(scene) {
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::SetGravity(Vector3 gravity) {
    m_gravity = gravity;
    if (m_simulation) {
        m_simulation->SetGravity(gravity);
    }
}

void PhysicsWorld::SetFixedTimeStep(float seconds) {
    SPOT_ASSERT(seconds > 0.0f, "The fixed time step must be positive");
    m_fixedTimeStep = seconds;
}

int PhysicsWorld::Advance(float deltaTime) {
    m_timeLeft += deltaTime;
    const int due = static_cast<int>(m_timeLeft / m_fixedTimeStep);
    const int steps = std::min(due, MaxStepsPerUpdate);
    m_timeLeft -= static_cast<float>(steps) * m_fixedTimeStep;
    if (due > steps) {
        m_timeLeft = std::fmod(m_timeLeft, m_fixedTimeStep);
    }
    return steps;
}

void PhysicsWorld::Step() {
    RebuildChanged();
    if (m_bodies.empty()) {
        return;
    }
    FollowEntities();
    GetSimulation().Step(m_fixedTimeStep);
    FollowBodies();
}

void PhysicsWorld::MarkChanged(EntityId entity) {
    m_changed.insert(entity);
}

const PhysicsWorld::Body* PhysicsWorld::FindMovingBody(EntityId entity) {
    if (m_changed.erase(entity) > 0) {
        Rebuild(entity);
    }
    const auto found = m_bodies.find(entity);
    return found != m_bodies.end() && found->second.motion != PhysicsMotion::Static ? &found->second : nullptr;
}

Detail::PhysicsSimulation& PhysicsWorld::GetSimulation() {
    if (!m_simulation) {
        m_simulation = Detail::PhysicsSimulation::Create();
        m_simulation->SetGravity(m_gravity);
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
        if (found->second.motion == PhysicsMotion::Dynamic) {
            linearVelocity = m_simulation->GetLinearVelocity(found->second.id);
            angularVelocity = m_simulation->GetAngularVelocity(found->second.id);
            keepVelocity = true;
        }
        m_simulation->DestroyBody(found->second.id);
        m_bodies.erase(found);
    }

    Entity* entity = m_scene.GetEntity(entityId);
    if (!entity || !entity->IsActiveInHierarchy()) {
        return;
    }

    const Transform& transform = entity->GetTransform();
    const Vector3 scale = GetWorldScale(transform);
    Detail::PhysicsBodyDesc desc;
    for (const Collider* collider : entity->GetComponents<Collider>()) {
        if (!collider->IsActiveAndEnabled()) {
            continue;
        }
        if (desc.shapes.empty()) {
            desc.friction = collider->GetFriction();
            desc.restitution = collider->GetRestitution();
        }
        desc.shapes.push_back(collider->GetShape(scale));
    }

    Rigidbody* rigidbody = nullptr;
    for (Rigidbody* candidate : entity->GetComponents<Rigidbody>()) {
        if (candidate->IsActiveAndEnabled()) {
            rigidbody = candidate;
            break;
        }
    }
    if (desc.shapes.empty() && !rigidbody) {
        return;
    }

    desc.position = transform.GetWorldPosition();
    desc.rotation = Normalize(transform.GetWorldRotation());
    if (rigidbody) {
        desc.motion = rigidbody->m_kinematic ? PhysicsMotion::Kinematic : PhysicsMotion::Dynamic;
        desc.mass = rigidbody->m_mass;
        desc.gravityScale = rigidbody->m_gravityScale;
        desc.linearDamping = rigidbody->m_linearDamping;
        desc.angularDamping = rigidbody->m_angularDamping;
        desc.linearVelocity = keepVelocity ? linearVelocity : rigidbody->m_linearVelocity;
        desc.angularVelocity = keepVelocity ? angularVelocity : rigidbody->m_angularVelocity;
        rigidbody->m_linearVelocity = {};
        rigidbody->m_angularVelocity = {};
    }

    const PhysicsBodyId id = GetSimulation().CreateBody(desc);
    if (id == PhysicsBodyId::Invalid) {
        return;
    }
    m_bodies.emplace(entityId, Body{.id = id,
                                    .motion = desc.motion,
                                    .position = desc.position,
                                    .rotation = transform.GetWorldRotation(),
                                    .scale = scale});
}

void PhysicsWorld::FollowEntities() {
    std::vector<EntityId> outdated;
    for (auto& [entityId, body] : m_bodies) {
        const Entity* entity = m_scene.GetEntity(entityId);
        if (!entity || !entity->IsActiveInHierarchy()) {
            outdated.push_back(entityId);
            continue;
        }

        const Transform& transform = entity->GetTransform();
        if (GetWorldScale(transform) != body.scale) {
            outdated.push_back(entityId);
            continue;
        }

        const Vector3 position = transform.GetWorldPosition();
        const Quaternion& rotation = transform.GetWorldRotation();
        if (body.motion == PhysicsMotion::Kinematic) {
            // Every step, so the body also stops once its entity does.
            m_simulation->MoveKinematic(body.id, position, Normalize(rotation), m_fixedTimeStep);
        } else if (position != body.position || rotation != body.rotation) {
            m_simulation->SetPose(body.id, position, Normalize(rotation));
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
        if (body.motion != PhysicsMotion::Dynamic || !m_simulation->IsAwake(body.id)) {
            continue;
        }
        Entity* entity = m_scene.GetEntity(entityId);
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
