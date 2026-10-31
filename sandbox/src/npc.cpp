#include "npc.h"

#include <spot/scene/audio/audio_system.h>
#include <spot/scene/physics/physics_world.h>

#include "gameplay/core/players.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/effects/surface.h"
#include "gameplay/health/health.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/weapons/firearm.h"

#include <algorithm>
#include <array>
#include <cmath>

SPOT_COMPONENT(Npc);

namespace {

constexpr float ThinkInterval = 0.3f;
// Seconds it keeps after what it fights out of sight, at the last place it saw it.
constexpr float SearchTime = 6.0f;

bool IsPlayer(Spot::Scene& scene, const Gameplay::Pawn& pawn) {
    for (std::size_t index = 0; index < Gameplay::Players::GetAll(scene).size(); ++index) {
        if (Gameplay::Players::GetPawn(scene, index) == &pawn || Gameplay::Players::GetBody(scene, index) == &pawn) {
            return true;
        }
    }
    return false;
}

// Where shots at the entity go: the chest of a character, the middle of anything else.
Spot::Vector3 AimPoint(const Spot::Entity& entity) {
    if (entity.HasComponent<Spot::CharacterController>()) {
        if (const auto* pawn = Gameplay::Pawn::Of(entity)) {
            return pawn->GetViewPoint().position - Spot::Vector3{0, 0.35f, 0};
        }
    }
    return entity.GetTransform().GetWorldPosition() + Spot::Vector3{0, 0.8f, 0};
}

Spot::Vector3 Flat(Spot::Vector3 vector) {
    return {vector.x, 0, vector.z};
}

} // namespace

void Npc::OnStart() {
    if (!m_hasHome) {
        m_home = GetEntity().GetTransform().GetWorldPosition();
        m_hasHome = true;
    }
    if (const auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>()) {
        m_dead = m_dead || (ragdoll->IsActive() && m_down <= 0);
    }
    SetState("Dead", m_dead);
    if (auto* health = GetEntity().GetComponent<Gameplay::Health>()) {
        m_died = health->OnDied.Connect([this](const Gameplay::DamageInfo& damage) {
            if (m_dead) {
                return;
            }
            // Knocked down by the same blow, it was pushed already.
            const bool pushed = m_down > 0;
            Die();
            if (auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>(); ragdoll && ragdoll->IsActive() && !pushed) {
                ragdoll->AddImpulse(damage.impulse, damage.point);
            }
            m_killedNow = true;
        });
    }
    if (auto* inventory = GetEntity().GetComponent<Gameplay::Inventory>();
        inventory && inventory->GetItemCount() == 0) {
        for (const auto& item : loadout) {
            inventory->AddFromPrefab(item);
        }
    }
    // Not all at once, when many start together.
    m_think = std::uniform_real_distribution<float>(0, ThinkInterval)(m_random);
}

bool Npc::IsAlive(const Spot::Entity& entity) const {
    if (const auto* npc = entity.GetComponent<Npc>()) {
        return !npc->IsDead();
    }
    if (const auto* health = entity.GetComponent<Gameplay::Health>(); health && health->IsDead()) {
        return false;
    }
    const auto* pawn = Pawn::Of(entity);
    return !pawn || !pawn->HasState("Dead");
}

bool Npc::IsHostileTo(const Spot::Entity& entity) const {
    if (&entity == &GetEntity()) {
        return false;
    }
    if (std::ranges::find(m_provokers, entity.GetId()) != m_provokers.end()) {
        return true;
    }
    const Pawn* pawn = Pawn::Of(entity);
    if (!pawn) {
        return false;
    }
    if (const auto* other = dynamic_cast<const Npc*>(pawn)) {
        return (attitude == Attitude::Enemy && other->attitude == Attitude::Ally) ||
               (attitude == Attitude::Ally && other->attitude == Attitude::Enemy);
    }
    return attitude == Attitude::Enemy && IsPlayer(GetScene(), *pawn);
}

bool Npc::CanSee(const Spot::Entity& target, Spot::Vector3 point) const {
    const Spot::Vector3 from = Pawn::GetViewPoint().position;
    const Spot::Vector3 toward = point - from;
    const float distance = Spot::Length(toward);
    if (distance < 0.01f) {
        return true;
    }
    const auto hit = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(from, toward / distance, distance,
                                                                             {.ignore = GetEntity().GetId()});
    if (!hit || hit->distance > distance - 0.5f) {
        return true;
    }
    const Spot::Entity* blocker = GetScene().GetEntity(hit->entity);
    for (; blocker; blocker = blocker->GetParent()) {
        if (blocker == &target) {
            return true;
        }
    }
    return false;
}

void Npc::Think() {
    std::erase_if(m_provokers, [this](Spot::EntityId id) {
        const Spot::Entity* entity = GetScene().GetEntity(id);
        return !entity || !IsAlive(*entity);
    });
    const Spot::Vector3 from = Pawn::GetViewPoint().position;
    Spot::Entity* best = nullptr;
    float nearest = sightRange;
    const auto consider = [&](Spot::Entity* entity) {
        if (!entity || !IsAlive(*entity) || !IsHostileTo(*entity)) {
            return;
        }
        const Spot::Vector3 point = AimPoint(*entity);
        const float distance = Spot::Distance(from, point);
        if (distance < nearest && CanSee(*entity, point)) {
            best = entity;
            nearest = distance;
        }
    };
    for (std::size_t index = 0; index < Gameplay::Players::GetAll(GetScene()).size(); ++index) {
        if (Pawn* pawn = Gameplay::Players::GetPawn(GetScene(), index)) {
            consider(&pawn->GetEntity());
        }
    }
    GetScene().ForEachComponent<Npc>([&](Npc& npc) { consider(&npc.GetEntity()); });
    for (const Spot::EntityId id : std::vector(m_provokers)) {
        consider(GetScene().GetEntity(id));
    }
    if (best) {
        if (best->GetId() != m_target || !m_seesTarget) {
            m_attackDelay = std::max(m_attackDelay, reactionTime);
        }
        m_target = best->GetId();
        m_lastSeen = AimPoint(*best);
        m_seesTarget = true;
        m_lost = 0;
        return;
    }
    // Out of sight: it looks where it saw it last, for a while.
    const Spot::Entity* target = GetScene().GetEntity(m_target);
    if (!target || !IsAlive(*target) || !IsHostileTo(*target) || m_lost > SearchTime) {
        m_target = Spot::EntityId::Invalid;
    }
    m_seesTarget = false;
}

void Npc::OnUpdate(float deltaTime) {
    m_killedNow = false;
    auto* animator = GetEntity().GetComponent<Spot::Animator>();
    if (!animator || m_dead) {
        return;
    }
    // The server thinks for it; clients show it walking where the server moves it.
    if (!Spot::Network::HasAuthority(GetEntity())) {
        animator->SetFloat("speed", m_speed);
        return;
    }
    if (m_down > 0) {
        m_down -= deltaTime;
        auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>();
        if (m_down <= 0 && (!ragdoll || ragdoll->SetActive(false, 0.35f))) {
            m_down = 0;
            m_fall = 0;
            m_think = 0;
        }
        m_down = m_down <= 0 && ragdoll && ragdoll->IsActive() ? 0.5f : m_down;
        return;
    }
    m_attackDelay = std::max(m_attackDelay - deltaTime, 0.0f);
    m_lost += m_seesTarget ? 0 : deltaTime;
    m_think -= deltaTime;
    if (m_think <= 0) {
        m_think = ThinkInterval;
        Think();
    }
    Move move;
    if (Spot::Entity* target = GetScene().GetEntity(m_target)) {
        move = Fight(*target);
    } else if (attitude == Attitude::Ally && Gameplay::Players::GetBody(GetScene())) {
        move = Follow();
        UseItem(false);
    } else {
        move = Patrol(deltaTime);
        UseItem(false);
    }
    m_speed = move.speed;
    animator->SetFloat("speed", move.speed);
    if (const auto listener = GetScene().GetOrAddSystem<Spot::AudioSystem>().FindListener()) {
        const float away = Spot::Distance(listener->position, GetEntity().GetTransform().GetWorldPosition());
        animator->SetUpdateInterval(away < animationNear ? 0.0f : away < animationFar ? 1.0f / 30.0f : 1.0f / 12.0f);
    }
    if (move.facing && Spot::LengthSquared(*move.facing) > 0.01f) {
        auto& transform = GetEntity().GetTransform();
        transform.SetWorldRotation(Spot::Slerp(transform.GetWorldRotation(),
                                               Spot::Quaternion::LookRotation(Spot::Normalize(*move.facing)),
                                               std::clamp(turnSpeed * deltaTime, 0.0f, 1.0f)));
    }
}

Npc::Move Npc::Patrol(float deltaTime) {
    auto axis = Spot::Normalize(Spot::Vector3{patrolAxis.x, 0, patrolAxis.z});
    if (Spot::LengthSquared(axis) < 0.01f) {
        axis = {0, 0, 1};
    }
    const auto destination = m_home + axis * (m_positive ? patrolDistance : -patrolDistance);
    const auto direction = Flat(destination - GetEntity().GetTransform().GetWorldPosition());
    const float distance = Spot::Length(direction);
    if (distance < 0.5f) {
        m_positive = !m_positive;
        m_pause = 0.6f;
    }
    m_pause = std::max(m_pause - deltaTime, 0.0f);
    Move move;
    move.speed = patrolDistance <= 0 || m_pause > 0 ? 0.0f : distance > 2 ? 2.0f : 1.0f;
    if (distance > 0.1f) {
        move.facing = direction;
    }
    return move;
}

Npc::Move Npc::Follow() {
    const Pawn* player = Gameplay::Players::GetPawn(GetScene());
    if (!player) {
        return {};
    }
    const auto direction =
        Flat(player->GetEntity().GetTransform().GetWorldPosition() - GetEntity().GetTransform().GetWorldPosition());
    const float distance = Spot::Length(direction);
    if (distance <= followDistance) {
        return {};
    }
    // Patrols around where it was left.
    m_home = GetEntity().GetTransform().GetWorldPosition();
    return {direction, distance > followDistance * 3 ? 2.0f : 1.0f};
}

Npc::Move Npc::Fight(Spot::Entity& target) {
    const Spot::Vector3 position = GetEntity().GetTransform().GetWorldPosition();
    const Spot::Vector3 point = m_seesTarget ? AimPoint(target) : m_lastSeen;
    const Spot::Vector3 direction = Flat(point - position);
    const float distance = Spot::Length(direction);
    auto* inventory = GetEntity().GetComponent<Gameplay::Inventory>();
    auto* firearm = inventory ? dynamic_cast<Gameplay::Firearm*>(inventory->GetEquipped()) : nullptr;
    if (!m_seesTarget) {
        UseItem(false);
        // Nothing where it was seen last: it gives up.
        if (distance < 1.0f) {
            m_target = Spot::EntityId::Invalid;
            return {};
        }
        return {direction, 2.0f};
    }
    const float reach = firearm ? attackRange : meleeRange;
    if (distance > reach) {
        UseItem(false);
        return {direction, distance > reach + 6 ? 2.0f : 1.0f};
    }
    const Spot::Vector3 forward = GetEntity().GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1};
    const bool facing = distance < 0.01f || Spot::Dot(Spot::Normalize(Flat(forward)), direction / distance) > 0.8f;
    if (!facing || m_attackDelay > 0) {
        UseItem(false);
        return {direction, 0.0f};
    }
    if (firearm) {
        if (firearm->GetAmmo() == 0 && firearm->GetReserve() == 0 && !firearm->IsReloading()) {
            firearm->Refill();
        }
        const Spot::Vector3 from = Pawn::GetViewPoint().position;
        std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
        const Spot::Vector3 stray = Spot::Vector3{unit(m_random), unit(m_random), unit(m_random)} * aimError;
        m_aim = Spot::Normalize(Spot::Normalize(point - from) + stray);
        const int ammo = firearm->GetAmmo();
        const float damage = firearm->damage;
        firearm->damage = damage * damageScale;
        UseItem(true);
        firearm->damage = damage;
        m_aim.reset();
        if (firearm->GetAmmo() < ammo) {
            m_attackDelay = std::max(firearm->fireInterval, shotPause * (1.0f + unit(m_random) * 0.5f));
        }
    } else {
        const Spot::Vector3 push = Spot::Normalize(point - position) * 40.0f;
        Gameplay::Damage::Apply(GetScene(), target.GetId(),
                                {.amount = meleeDamage,
                                 .type = "Melee",
                                 .point = point,
                                 .impulse = push,
                                 .instigator = GetEntity().GetId(),
                                 .source = GetEntity().GetId()});
        m_attackDelay = meleeInterval;
    }
    return {direction, 0.0f};
}

void Npc::UseItem(bool fire) {
    auto* inventory = GetEntity().GetComponent<Gameplay::Inventory>();
    if (!inventory || inventory->GetItemCount() == 0) {
        return;
    }
    Gameplay::ItemInput input;
    input.active = input.canUse = true;
    input.Set("Fire", fire, fire);
    inventory->Use(input);
}

Gameplay::ViewPoint Npc::GetViewPoint() const {
    Gameplay::ViewPoint view = Pawn::GetViewPoint();
    if (m_aim) {
        view.direction = *m_aim;
        view.rotation = Spot::Quaternion::LookRotation(*m_aim);
    }
    return view;
}

void Npc::OnFixedUpdate(float deltaTime) {
    auto* character = GetEntity().GetComponent<Spot::CharacterController>();
    if (!Spot::Network::HasAuthority(GetEntity()) || m_dead || m_down > 0 || !character || !character->IsActiveAndEnabled()) {
        return;
    }
    m_fall = character->IsGrounded()
                 ? 0
                 : m_fall + GetScene().GetOrAddSystem<Spot::PhysicsWorld>().GetGravity().y * deltaTime;
    character->Move({0, m_fall, 0}, deltaTime);
}

void Npc::OnAnimationEvent(const Spot::AnimationEvent& event) {
    if (event.name != "Footstep" || m_dead) {
        return;
    }
    ++m_footsteps;
    const auto* library = Gameplay::SurfaceLibrary::Find(GetScene());
    const auto* sounds = library ? library->Get(footstepSurface) : nullptr;
    if (sounds && !sounds->footsteps.empty()) {
        const auto& sound = sounds->footsteps[(m_footsteps - 1) % sounds->footsteps.size()];
        Gameplay::EffectLibrary::PlaySound(
            GetScene(), sound, GetEntity().GetTransform().GetWorldPosition(),
            {.volume = 0.25f,
             .bus = "Effects",
             .spatialSettings = {.minDistance = 1, .maxDistance = 20},
             .maxInstances = 3});
    }
}

void Npc::KnockDown(const Gameplay::DamageInfo& damage) {
    auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>();
    if (!ragdoll || (!ragdoll->IsActive() && !ragdoll->SetActive(true))) {
        return;
    }
    m_down = knockdownTime;
    m_aim.reset();
    ragdoll->AddImpulse(damage.impulse, damage.point);
}

void Npc::Die() {
    m_dead = true;
    m_down = 0;
    SetState("Dead", true);
    m_target = Spot::EntityId::Invalid;
    m_aim.reset();
    if (auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>(); ragdoll && !ragdoll->IsActive()) {
        ragdoll->SetActive(true);
    }
}

void Npc::Hit(Spot::Vector3 impulse, Spot::Vector3 point) {
    if (m_dead) {
        return;
    }
    if (auto* health = GetEntity().GetComponent<Gameplay::Health>(); health && !health->IsDead()) {
        health->TakeDamage({.amount = health->GetHealth(), .type = "Hit", .point = point, .impulse = impulse});
    }
    if (!m_dead) {
        Die();
        if (auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>(); ragdoll && ragdoll->IsActive()) {
            ragdoll->AddImpulse(impulse, point);
        }
    }
}

void Npc::OnDamage(const Gameplay::DamageInfo& damage) {
    if (!Spot::Network::HasAuthority(GetEntity())) {
        return;
    }
    if (m_dead) {
        // Its Health, which took the damage first, already pushed it with the blow that killed it.
        if (auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>(); ragdoll && ragdoll->IsActive() && !m_killedNow) {
            ragdoll->AddImpulse(damage.impulse, damage.point);
        }
        return;
    }
    if (m_down > 0) {
        if (auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>(); ragdoll && ragdoll->IsActive()) {
            ragdoll->AddImpulse(damage.impulse, damage.point);
        }
    } else if (std::ranges::find(knockdownTypes, damage.type) != knockdownTypes.end()) {
        KnockDown(damage);
    }
    if (!GetEntity().HasComponent<Gameplay::Health>()) {
        Hit(damage.impulse, damage.point);
        return;
    }
    // It fights back whoever hurt it, unless they are on its side.
    const Spot::Entity* attacker = GetScene().GetEntity(damage.instigator);
    const Pawn* pawn = attacker ? Pawn::Of(*attacker) : nullptr;
    if (!pawn || pawn == this) {
        return;
    }
    const auto* other = dynamic_cast<const Npc*>(pawn);
    const bool friendly = other ? other->attitude == attitude && attitude != Attitude::Neutral
                                : attitude == Attitude::Ally && IsPlayer(GetScene(), *pawn);
    if (!friendly && std::ranges::find(m_provokers, pawn->GetEntity().GetId()) == m_provokers.end()) {
        m_provokers.push_back(pawn->GetEntity().GetId());
        m_think = 0;
    }
}

void Npc::Revive() {
    if (auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>(); ragdoll && ragdoll->SetActive(false, 0.35f)) {
        m_dead = false;
        m_down = 0;
        SetState("Dead", false);
        m_fall = 0;
        m_pause = 0.6f;
        m_provokers.clear();
        m_target = Spot::EntityId::Invalid;
        if (auto* health = GetEntity().GetComponent<Gameplay::Health>()) {
            health->Restore();
        }
    }
}

void Npc::Replicate(Spot::Archive& archive) {
    Pawn::Replicate(archive);
    float speed = m_speed;
    bool dead = m_dead;
    bool down = m_down > 0;
    archive.Field("speed", speed);
    archive.Field("dead", dead);
    archive.Field("down", down);
    if (!archive.IsLoading()) {
        return;
    }
    m_speed = speed;
    if (dead && !m_dead) {
        Die();
    } else if (!dead && m_dead) {
        Revive();
    }
    // Knocked down, it falls limp as the server has it, and gets up as it does.
    auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>();
    if (!m_dead && ragdoll && down != ragdoll->IsActive()) {
        ragdoll->SetActive(down, down ? 0.0f : 0.35f);
    }
}

void Npc::Serialize(Spot::Archive& archive) {
    static constexpr std::array<std::string_view, 3> Attitudes = {"Ally", "Neutral", "Enemy"};
    Pawn::Serialize(archive);
    archive.Field("attitude", attitude,
                  {.enumNames = Attitudes, .tooltip = "Enemies fight players and allies; neutrals fight back only"});
    archive.Field("loadout", loadout, {.tooltip = "Items carried by a newly spawned NPC"});
    archive.Field("patrolAxis", patrolAxis);
    archive.Field("patrolDistance", patrolDistance);
    archive.Field("turnSpeed", turnSpeed);
    archive.Field("sightRange", sightRange, {.min = 0, .max = 1000});
    archive.Field("attackRange", attackRange, {.min = 0, .max = 1000, .tooltip = "Shoots from within this"});
    archive.Field("reactionTime", reactionTime, {.min = 0, .max = 10, .tooltip = "Seconds before the first shot"});
    archive.Field("shotPause", shotPause, {.min = 0, .max = 10, .tooltip = "Least seconds between shots"});
    archive.Field("aimError", aimError, {.min = 0, .max = 1, .tooltip = "Radians shots may stray"});
    archive.Field("damageScale", damageScale, {.min = 0, .max = 10, .tooltip = "Share of its firearm's damage"});
    archive.Field("knockdownTypes", knockdownTypes, {.tooltip = "Damage types that knock it down"});
    archive.Field("knockdownTime", knockdownTime, {.min = 0, .max = 60, .tooltip = "Seconds before it gets up"});
    archive.Field("meleeRange", meleeRange, {.min = 0, .max = 10});
    archive.Field("meleeDamage", meleeDamage, {.min = 0, .max = 1000});
    archive.Field("meleeInterval", meleeInterval, {.min = 0, .max = 10});
    archive.Field("followDistance", followDistance, {.min = 0, .max = 100, .tooltip = "How close allies keep"});
    archive.Field("animationNear", animationNear,
                  {.min = 0, .max = 1000, .tooltip = "Animated every frame within this"});
    archive.Field("animationFar", animationFar,
                  {.min = 0, .max = 1000, .tooltip = "Animated 12 times a second beyond this"});
    archive.Field("footstepSurface", footstepSurface, {.tooltip = "The kind of surface its steps sound like"});
    archive.Field("dead", m_dead);
    archive.Field("home", m_home, {.hidden = true});
    archive.Field("hasHome", m_hasHome, {.hidden = true});
    archive.Field("positive", m_positive, {.hidden = true});
    archive.Field("pause", m_pause, {.hidden = true});
    archive.Field("fall", m_fall, {.hidden = true});
    archive.Field("down", m_down, {.hidden = true});
    archive.Field("provokers", m_provokers, {.hidden = true});
}
