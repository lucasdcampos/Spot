#include "spot/scene/physics/collision_listener.h"
#include "spot/scene/physics/physics_world.h"

#include "spot/scene/physics/character_controller.h"
#include "spot/scene/scene.h"

#include <algorithm>
#include <utility>

namespace Spot {

namespace {

// The contact as the first or second entity sees it: the normal points toward it and the velocity is its own minus
// the other's.
Collision MakeCollision(const Contact3D& contact, EntityId other, bool forFirst) {
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
    return found != m_bodies.end() && found->second.character != CharacterId3D::Invalid ? &found->second : nullptr;
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

std::optional<CharacterState3D> PhysicsWorld::GetCharacterState(EntityId entity) {
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
        if (body.character == CharacterId3D::Invalid) {
            continue;
        }
        const Vector3 velocity = m_simulation->GetCharacterState(body.character).velocity;
        for (const CharacterContact3D& contact : body.characterContacts) {
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
    for (const Contact3D& contact : m_contactBuffer) {
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
                component.RunGuarded(hook, [&] { call(component); });
            }
        });
    };
    const ContactPair& pair = contact.pair;
    if (pair.trigger) {
        const TriggerContact forFirst{.other = pair.second, .isTrigger = pair.firstIsTrigger};
        const TriggerContact forSecond{.other = pair.first, .isTrigger = !pair.firstIsTrigger};
        if (phase == ContactPhase::Enter) {
            notify(pair.first, "OnTriggerEnter", [&](Component& c) {
                if (auto* listener = dynamic_cast<TriggerListener*>(&c))
                    listener->OnTriggerEnter(forFirst);
            });
            notify(pair.second, "OnTriggerEnter", [&](Component& c) {
                if (auto* listener = dynamic_cast<TriggerListener*>(&c))
                    listener->OnTriggerEnter(forSecond);
            });
        } else if (phase == ContactPhase::Exit) {
            notify(pair.first, "OnTriggerExit", [&](Component& c) {
                if (auto* listener = dynamic_cast<TriggerListener*>(&c))
                    listener->OnTriggerExit(forFirst);
            });
            notify(pair.second, "OnTriggerExit", [&](Component& c) {
                if (auto* listener = dynamic_cast<TriggerListener*>(&c))
                    listener->OnTriggerExit(forSecond);
            });
        }
        return;
    }
    const auto deliver = [&](EntityId id, const Collision& seen) {
        switch (phase) {
        case ContactPhase::Enter:
            notify(id, "OnCollisionEnter", [&](Component& c) {
                if (auto* listener = dynamic_cast<CollisionListener*>(&c))
                    listener->OnCollisionEnter(seen);
            });
            break;
        case ContactPhase::Stay:
            notify(id, "OnCollisionStay", [&](Component& c) {
                if (auto* listener = dynamic_cast<CollisionListener*>(&c))
                    listener->OnCollisionStay(seen);
            });
            break;
        case ContactPhase::Exit:
            notify(id, "OnCollisionExit", [&](Component& c) {
                if (auto* listener = dynamic_cast<CollisionListener*>(&c))
                    listener->OnCollisionExit(seen);
            });
            break;
        }
    };
    deliver(pair.first, contact.collision);
    deliver(pair.second, Flip(contact.collision, pair.first));
}

} // namespace Spot
