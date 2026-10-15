#include "scene/physics/physics_world.h"

#include "scene/physics/joint.h"
#include "scene/scene.h"

#include <algorithm>
#include <vector>

namespace Spot {

using Detail::PhysicsBodyId;
using Detail::PhysicsMotion;

void PhysicsWorld::MarkJointChanged(Joint& joint) {
    ForgetJoint(joint);
    if (joint.IsActiveAndEnabled()) {
        m_pendingJoints.push_back(&joint);
    }
}

void PhysicsWorld::ForgetJoint(Joint& joint) {
    std::erase(m_pendingJoints, &joint);
    if (const auto found = m_joints.find(&joint); found != m_joints.end()) {
        m_simulation->DestroyJoint(found->second.id);
        m_joints.erase(found);
    }
}

Detail::PhysicsJointId PhysicsWorld::FindJoint(const Joint& joint) const {
    const auto found = m_joints.find(&joint);
    return found != m_joints.end() ? found->second.id : Detail::PhysicsJointId::Invalid;
}

float PhysicsWorld::GetJointForce(const Joint& joint) const {
    const Detail::PhysicsJointId id = FindJoint(joint);
    return id != Detail::PhysicsJointId::Invalid ? m_simulation->GetJointForce(id, m_stepTime) : 0.0f;
}

float PhysicsWorld::GetHingeAngle(const Joint& joint) const {
    const Detail::PhysicsJointId id = FindJoint(joint);
    return id != Detail::PhysicsJointId::Invalid ? m_simulation->GetHingeAngle(id) : 0.0f;
}

void PhysicsWorld::SetHingeMotor(const Joint& joint, bool enabled, float speed, float torque) {
    if (const Detail::PhysicsJointId id = FindJoint(joint); id != Detail::PhysicsJointId::Invalid) {
        m_simulation->SetHingeMotor(id, enabled, speed, torque);
    }
}

void PhysicsWorld::ReleaseJoints(EntityId entity) {
    for (auto it = m_joints.begin(); it != m_joints.end();) {
        if (it->second.owner == entity || it->second.connected == entity) {
            m_simulation->DestroyJoint(it->second.id);
            m_pendingJoints.push_back(const_cast<Joint*>(it->first));
            it = m_joints.erase(it);
        } else {
            ++it;
        }
    }
}

void PhysicsWorld::CreatePendingJoints() {
    std::erase_if(m_pendingJoints, [this](Joint* joint) {
        const EntityId owner = joint->GetEntity().GetId();
        const auto ownerBody = m_bodies.find(owner);
        if (ownerBody == m_bodies.end() || ownerBody->second.id == PhysicsBodyId::Invalid) {
            // It waits for its entity's body.
            return false;
        }
        // The entity's body is the second, so angles and motors are its own, relative to what it is joined to.
        Detail::PhysicsJointDesc desc{.second = ownerBody->second.id};
        const Transform* connected = nullptr;
        bool connectedMoves = false;
        if (joint->m_connected != EntityId::Invalid) {
            const auto connectedBody = m_bodies.find(joint->m_connected);
            const Entity* entity = GetScene().GetEntity(joint->m_connected);
            if (!entity || connectedBody == m_bodies.end() || connectedBody->second.id == PhysicsBodyId::Invalid ||
                joint->m_connected == owner) {
                // It waits for the other entity's body.
                return false;
            }
            desc.first = connectedBody->second.id;
            connected = &entity->GetTransform();
            connectedMoves = connectedBody->second.motion != PhysicsMotion::Static;
        }
        if (ownerBody->second.motion == PhysicsMotion::Static && !connectedMoves) {
            // Nothing it joins can move, so it has nothing to do.
            return true;
        }
        joint->Describe(desc, joint->GetEntity().GetTransform(), connected);
        const Detail::PhysicsJointId id = m_simulation->CreateJoint(desc);
        if (id != Detail::PhysicsJointId::Invalid) {
            m_joints.emplace(joint, JointRecord{id, owner, joint->m_connected});
        }
        return true;
    });
}

void PhysicsWorld::BreakJoints() {
    std::vector<Joint*> broken;
    for (const auto& [joint, record] : m_joints) {
        if (joint->m_breakForce > 0.0f && m_simulation->GetJointForce(record.id, m_stepTime) > joint->m_breakForce) {
            broken.push_back(const_cast<Joint*>(joint));
        }
    }
    // In the order of their entities, so runs are reproducible.
    std::ranges::sort(broken,
                      [](const Joint* a, const Joint* b) { return a->GetEntity().GetId() < b->GetEntity().GetId(); });
    for (Joint* joint : broken) {
        ForgetJoint(*joint);
        // Enabling it again mends it.
        joint->m_broken = true;
        joint->SetEnabled(false);
        joint->GetEntity().ForEachComponent<Component>([joint](Component& component) {
            if (component.IsActiveAndEnabled()) {
                component.InvokeHook("OnJointBreak", [&] { component.OnJointBreak(*joint); });
            }
        });
    }
}

} // namespace Spot
