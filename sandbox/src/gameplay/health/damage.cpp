#include "gameplay/health/damage.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/core/body_authority.h"

#include <algorithm>
#include <vector>

namespace Gameplay {

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

bool HasReceivers(Spot::Entity& entity) {
    bool found = false;
    entity.ForEachComponent<Spot::Component>(
        [&](Spot::Component& component) { found = found || dynamic_cast<DamageReceiver*>(&component) != nullptr; });
    return found;
}

struct Owner {
    Spot::Entity* entity = nullptr;
    // Its ragdoll's body was hit, which its receivers push.
    bool ragdoll = false;
};

// Whose receivers take what reaches the entity: its own or its nearest ancestor's, or those of the ragdoll whose body
// it is.
Owner FindOwner(Spot::Scene& scene, Spot::Entity& entity) {
    for (Spot::Entity* at = &entity; at; at = at->GetParent()) {
        if (HasReceivers(*at)) {
            return {at, false};
        }
    }
    Owner owner;
    scene.ForEachComponent<Spot::Ragdoll>([&](Spot::Ragdoll& ragdoll) {
        if (!owner.entity && ragdoll.OwnsBody(entity.GetId())) {
            for (Spot::Entity* at = &ragdoll.GetEntity(); at; at = at->GetParent()) {
                if (HasReceivers(*at)) {
                    owner = {at, true};
                    break;
                }
            }
        }
    });
    return owner;
}

bool Give(Spot::Entity& owner, const DamageInfo& damage) {
    bool given = false;
    owner.ForEachComponent<Spot::Component>([&](Spot::Component& component) {
        if (auto* receiver = dynamic_cast<DamageReceiver*>(&component); receiver && component.IsActiveAndEnabled()) {
            receiver->OnDamage(damage);
            given = true;
        }
    });
    return given;
}

} // namespace

DamageSystem& DamageSystem::Get(Spot::Scene& scene) {
    return scene.GetOrAddSystem<DamageSystem>();
}

namespace {

// Damage a server applied, for its clients to hear of.
struct DamageReport {
    static constexpr std::string_view NetName = "Gameplay.Damage";
    bool killed = false;
    Spot::EntityId target = Spot::EntityId::Invalid;
    float amount = 0.0f;
    std::string type;
    Spot::Vector3 point;
    Spot::Vector3 impulse;
    Spot::EntityId instigator = Spot::EntityId::Invalid;
    Spot::EntityId source = Spot::EntityId::Invalid;

    void Serialize(Spot::Archive& archive) {
        archive.Field("killed", killed);
        archive.Field("target", target);
        archive.Field("amount", amount);
        archive.Field("type", type);
        archive.Field("point", point);
        archive.Field("impulse", impulse);
        archive.Field("instigator", instigator);
        archive.Field("source", source);
    }
};

} // namespace

void DamageSystem::Report(const DamageApplied& applied, bool killed) {
    (killed ? OnKilled : OnDamageApplied).Emit(applied);
    Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
    if (!system || !system->IsServer()) {
        return;
    }
    const DamageReport report{.killed = killed,
                              .target =
                                  [&] {
                                      auto* entity = GetScene().GetEntity(applied.target);
                                      const Owner owner = entity ? FindOwner(GetScene(), *entity) : Owner{};
                                      return owner.entity ? owner.entity->GetId() : applied.target;
                                  }(),
                              .amount = applied.damage.amount,
                              .type = applied.damage.type,
                              .point = applied.damage.point,
                              .impulse = applied.damage.impulse,
                              .instigator = applied.damage.instigator,
                              .source = applied.damage.source};
    for (const Spot::NetPeerId peer : system->GetReadyPeers()) {
        system->GetSession()->Send(peer, report, Spot::NetDelivery::Unreliable);
    }
}

void DamageSystem::OnUpdate(float /*deltaTime*/) {
    const Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
    Spot::NetworkSession* session = system && system->IsClient() ? system->GetSession() : nullptr;
    if (!session || session == m_session) {
        return;
    }
    m_session = session;
    session->GetMessages().On<DamageReport>([session](Spot::NetPeerId, DamageReport& report) {
        Spot::NetworkSystem* bound = session->GetSystem();
        if (!bound) {
            return;
        }
        const DamageApplied applied{.target = report.target,
                                    .damage = {.amount = report.amount,
                                               .type = report.type,
                                               .point = report.point,
                                               .impulse = report.impulse,
                                               .instigator = report.instigator,
                                               .source = report.source}};
        DamageSystem& damage = Get(bound->GetScene());
        (report.killed ? damage.OnKilled : damage.OnDamageApplied).Emit(applied);
    });
}

namespace Damage {

bool Apply(Spot::Scene& scene, Spot::EntityId target, const DamageInfo& damage) {
    Spot::Entity* entity = scene.GetEntity(target);
    if (!entity) {
        return false;
    }
    const Owner owner = FindOwner(scene, *entity);
    bool took = owner.entity && Give(*owner.entity, damage);
    // The entity may be gone now, as a receiver may destroy what it is on.
    entity = scene.GetEntity(target);
    auto* body = entity && !owner.ragdoll && damage.pushesBody ? entity->GetComponent<Spot::Rigidbody>() : nullptr;
    // A body a player's machine simulates comes back here to be pushed.
    if (body && body->IsKinematic() && BodyAuthority::GetHolder(*entity) != Spot::NetPeerId::Invalid) {
        BodyAuthority::TakeBack(*entity);
    }
    if (body && !body->IsKinematic() && Spot::LengthSquared(damage.impulse) > 0.0f) {
        body->AddImpulseAtPosition(damage.impulse, damage.point);
        took = true;
    }
    if (took) {
        DamageSystem::Get(scene).Report({target, damage}, false);
    }
    return took;
}

void ApplyRadial(Spot::Scene& scene, Spot::Vector3 center, const RadialDamage& damage) {
    if (!(damage.radius > 0.0f)) {
        return;
    }
    auto& physics = scene.GetOrAddSystem<Spot::PhysicsWorld>();
    const float radius = damage.radius;
    std::vector<Spot::EntityId> reached;
    const auto give = [&](Spot::Entity& owner, float share, Spot::Vector3 direction) {
        reached.push_back(owner.GetId());
        const DamageInfo info{.amount = damage.amount * share,
                              .type = damage.type,
                              .point = center,
                              .impulse = Spot::Normalize(direction + Up * 0.3f) * (damage.impulse * share),
                              .instigator = damage.instigator,
                              .source = damage.source,
                              .pushesBody = false};
        if (Give(owner, info)) {
            DamageSystem::Get(scene).Report({owner.GetId(), info}, false);
        }
    };

    // Characters by the nearest point of their capsules, which may be shielded.
    std::vector<Spot::CharacterController*> characters;
    scene.ForEachComponent<Spot::CharacterController>(
        [&](Spot::CharacterController& character) { characters.push_back(&character); });
    for (Spot::CharacterController* character : characters) {
        Spot::Entity& entity = character->GetEntity();
        if (entity.GetId() == damage.ignore || !HasReceivers(entity)) {
            continue;
        }
        const float half = character->GetHeight() * 0.5f;
        const float width = character->GetRadius();
        const Spot::Vector3 middle = entity.GetTransform().GetWorldPosition() + character->GetCenter();
        const Spot::Vector3 axis{middle.x, std::clamp(center.y, middle.y - half + width, middle.y + half - width),
                                 middle.z};
        const float toAxis = Spot::Distance(center, axis);
        const float distance = std::max(toAxis - width, 0.0f);
        if (distance >= radius) {
            continue;
        }
        float share = 1.0f - distance / radius;
        const Spot::Vector3 direction = toAxis > 0.01f ? (axis - center) / toAxis : Up;
        // From a little out of the center, past what exploded, to the character: anything between shields it.
        const float start = std::min(0.5f, distance);
        if (distance - start > 0.05f &&
            physics.Raycast(center + direction * start, direction, distance - start, {.ignore = entity.GetId()})) {
            share *= damage.cover;
        }
        give(entity, share, direction);
    }

    // Bodies by their positions, and the receivers of what they belong to.
    for (const Spot::OverlapHit& hit : physics.OverlapSphere(center, radius, {.ignore = damage.ignore})) {
        Spot::Entity* entity = scene.GetEntity(hit.entity);
        if (!entity) {
            continue;
        }
        const Spot::Vector3 offset = entity->GetTransform().GetWorldPosition() - center;
        const float distance = Spot::Length(offset);
        // Straight up from right below the center, and harder the closer to it, down to nothing at the radius.
        const Spot::Vector3 direction = distance > 0.01f ? offset / distance : Up;
        const float share = std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
        const Owner owner = FindOwner(scene, *entity);
        if (owner.entity && std::ranges::find(reached, owner.entity->GetId()) == reached.end()) {
            give(*owner.entity, share, direction);
        }
        entity = scene.GetEntity(hit.entity);
        auto* body = entity ? entity->GetComponent<Spot::Rigidbody>() : nullptr;
        if (body && !body->IsKinematic()) {
            // A little lift, so things fly rather than slide.
            body->AddImpulse(Spot::Normalize(direction + Up * 0.3f) * (damage.impulse * share));
        }
    }
}

} // namespace Damage

} // namespace Gameplay
