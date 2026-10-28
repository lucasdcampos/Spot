#include "npc.h"
#include <spot/scene/audio/audio_system.h>
#include <spot/scene/physics/physics_world.h>

#include "gameplay/effects/surface.h"
#include "gameplay/inventory/inventory.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(Npc);

void Npc::OnStart() {
    if (!m_hasHome) {
        m_home = GetEntity().GetTransform().GetWorldPosition();
        m_hasHome = true;
    }
    if (const auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>()) {
        m_dead = m_dead || ragdoll->IsActive();
    }
    SetState("Dead", m_dead);
    if (auto* inventory = GetEntity().GetComponent<Gameplay::Inventory>();
        inventory && inventory->GetItemCount() == 0) {
        for (const auto& item : loadout) {
            inventory->AddFromPrefab(item);
        }
    }
}

void Npc::OnUpdate(float deltaTime) {
    auto* animator = GetEntity().GetComponent<Spot::Animator>();
    if (!animator || m_dead) {
        return;
    }
    auto axis = Spot::Normalize(Spot::Vector3{patrolAxis.x, 0, patrolAxis.z});
    if (Spot::LengthSquared(axis) < 0.01f) {
        axis = {0, 0, 1};
    }
    const auto destination = m_home + axis * (m_positive ? patrolDistance : -patrolDistance);
    auto direction = destination - GetEntity().GetTransform().GetWorldPosition();
    direction.y = 0;
    const float distance = Spot::Length(direction);
    if (distance < 0.5f) {
        m_positive = !m_positive;
        m_pause = 0.6f;
    }
    m_pause = std::max(m_pause - deltaTime, 0.0f);
    animator->SetFloat("speed", patrolDistance <= 0 || m_pause > 0 ? 0.0f : distance > 2 ? 2.0f : 1.0f);
    if (const auto listener = GetScene().GetOrAddSystem<Spot::AudioSystem>().FindListener()) {
        const float away = Spot::Distance(listener->position, GetEntity().GetTransform().GetWorldPosition());
        animator->SetUpdateInterval(away < animationNear ? 0.0f : away < animationFar ? 1.0f / 30.0f : 1.0f / 12.0f);
    }
    if (distance > 0.1f) {
        auto& transform = GetEntity().GetTransform();
        transform.SetWorldRotation(Spot::Slerp(transform.GetWorldRotation(), Spot::Quaternion::LookRotation(direction),
                                               std::clamp(turnSpeed * deltaTime, 0.0f, 1.0f)));
    }
}

void Npc::OnFixedUpdate(float deltaTime) {
    auto* character = GetEntity().GetComponent<Spot::CharacterController>();
    if (m_dead || !character || !character->IsActiveAndEnabled()) {
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
        GetScene().GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
            sound.GetShared(), GetEntity().GetTransform().GetWorldPosition(),
            {.volume = 0.25f,
             .bus = "Effects",
             .spatialSettings = {.minDistance = 1, .maxDistance = 20},
             .maxInstances = 3});
    }
}

void Npc::Hit(Spot::Vector3 impulse, Spot::Vector3 point) {
    auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>();
    if (!ragdoll || (!ragdoll->IsActive() && !ragdoll->SetActive(true))) {
        return;
    }
    m_dead = true;
    SetState("Dead", true);
    ragdoll->AddImpulse(impulse, point);
}
void Npc::OnDamage(const Gameplay::DamageInfo& damage) {
    Hit(damage.impulse, damage.point);
}

void Npc::Revive() {
    if (auto* ragdoll = GetEntity().GetComponent<Spot::Ragdoll>(); ragdoll && ragdoll->SetActive(false, 0.35f)) {
        m_dead = false;
        SetState("Dead", false);
        m_fall = 0;
        m_pause = 0.6f;
    }
}
void Npc::Serialize(Spot::Archive& archive) {
    Pawn::Serialize(archive);
    archive.Field("loadout", loadout, {.tooltip = "Items carried by a newly spawned NPC"});
    archive.Field("patrolAxis", patrolAxis);
    archive.Field("patrolDistance", patrolDistance);
    archive.Field("turnSpeed", turnSpeed);
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
}
