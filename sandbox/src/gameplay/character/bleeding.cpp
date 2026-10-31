#include "gameplay/character/bleeding.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/effects/effect_library.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(Bleeding);

void Bleeding::OnStart() {
    m_damage = DamageSystem::Get(GetScene()).OnDamageApplied.Connect([this](const DamageApplied& applied) {
        if (!Spot::Network::IsServer(GetScene()) && applied.target == GetEntity().GetId() && IsActiveAndEnabled()) {
            OnDamage(applied.damage);
        }
    });
}

namespace {

const Spot::Vector3 Down{0.0f, -1.0f, 0.0f};

} // namespace

float Bleeding::Random(float from, float to) {
    return std::uniform_real_distribution<float>(from, to)(m_random);
}

bool Bleeding::IsCharacter(const Spot::Entity* entity) {
    if (!entity) {
        return false;
    }
    for (const Spot::Entity* at = entity; at; at = at->GetParent()) {
        if (at->GetComponent<Bleeding>() || at->GetComponent<Spot::CharacterController>()) {
            return true;
        }
    }
    bool ragdoll = false;
    GetScene().ForEachComponent<Spot::Ragdoll>(
        [&](Spot::Ragdoll& owner) { ragdoll = ragdoll || owner.OwnsBody(entity->GetId()); });
    return ragdoll;
}

void Bleeding::OnDamage(const DamageInfo& damage) {
    if (damage.type != damageType || damage.amount <= 0.0f) {
        return;
    }
    Spot::Scene& scene = GetScene();
    const Spot::Vector3 along = Spot::LengthSquared(damage.impulse) > 1e-6f ? Spot::Normalize(damage.impulse) : Down;
    EffectLibrary::Play(scene, exitEffect, damage.point, along);

    const Spot::Vector3 side = Spot::Normalize(Spot::Cross(
        along, std::abs(along.y) < 0.99f ? Spot::Vector3{0.0f, 1.0f, 0.0f} : Spot::Vector3{1.0f, 0.0f, 0.0f}));
    const Spot::Vector3 up = Spot::Cross(side, along);
    for (int i = 0; i < spatterCount; ++i) {
        // Even over the cone's disc, and sinking as it flies.
        const float radius = std::tan(spatterCone) * std::sqrt(Random(0.0f, 1.0f));
        const float angle = Random(0.0f, 2.0f * Spot::Pi);
        const Spot::Vector3 direction = Spot::Normalize(
            along + (side * std::cos(angle) + up * std::sin(angle)) * radius + Down * Random(0.1f, 0.5f));
        Splash(spatter, damage.point, direction, spatterReach, Random(minSize, maxSize), true);
    }
    // Most of it falls close below, a pool and the drops around it.
    Splash(drip, damage.point, Down, dripReach, maxSize, false);
    for (int i = 0; i < dripCount; ++i) {
        const float angle = Random(0.0f, 2.0f * Spot::Pi);
        const float reach = dripRadius * std::sqrt(Random(0.0f, 1.0f));
        const Spot::Vector3 from = damage.point + Spot::Vector3{std::cos(angle), 0.0f, std::sin(angle)} * reach;
        Splash(drip, from, Down, dripReach, Random(minSize, maxSize) * 0.5f, false);
    }
}

bool Bleeding::Splash(const DecalStyle& style, Spot::Vector3 from, Spot::Vector3 direction, float reach, float size,
                      bool thrown) {
    Spot::Scene& scene = GetScene();
    for (const Spot::RaycastHit& hit : scene.GetOrAddSystem<Spot::PhysicsWorld>().RaycastAll(from, direction, reach)) {
        if (IsCharacter(scene.GetEntity(hit.entity))) {
            continue;
        }
        // Fresh blood is brighter and old darker, and thin films lighter than pools: no two marks alike.
        DecalStyle tinted = style;
        const float shade = Random(0.65f, 1.15f);
        tinted.color = {style.color.r * shade, style.color.g * shade, style.color.b * shade, style.color.a};
        float angle = Random(0.0f, 2.0f * Spot::Pi);
        float stretch = 1.0f;
        if (thrown) {
            // Spreads as it flies, and streaks the longer the flatter it meets the surface.
            size *= 0.6f + 0.8f * std::clamp(hit.distance / std::max(reach, 0.01f), 0.0f, 1.0f);
            const float facing = std::abs(Spot::Dot(Spot::Normalize(direction), hit.normal));
            stretch = std::clamp(1.0f / std::max(facing, 0.01f), 1.0f, 3.0f) * 1.5f;
            angle = Decals::AngleAlong(hit.normal, direction);
        }
        Decals::Get(scene).Place(tinted, hit.entity, hit.point, hit.normal, size, angle, stretch);
        return true;
    }
    return false;
}

void Bleeding::Serialize(Spot::Archive& archive) {
    archive.Field("spatter", spatter);
    archive.Field("drip", drip);
    archive.Field("exitEffect", exitEffect, {.tooltip = "Sprays out along the hit"});
    archive.Field("damageType", damageType, {.tooltip = "The type of damage that bleeds"});
    archive.Field("spatterCount", spatterCount, {.min = 0.0f, .max = 32.0f, .tooltip = "Marks thrown each hit"});
    archive.Field("spatterCone", spatterCone, {.min = 0.0f, .max = 1.5f, .angle = true, .tooltip = "Half angle"});
    archive.Field("spatterReach", spatterReach,
                  {.min = 0.0f, .max = 20.0f, .speed = 0.05f, .tooltip = "Meters behind the hit blood reaches"});
    archive.Field("dripReach", dripReach,
                  {.min = 0.0f, .max = 20.0f, .speed = 0.05f, .tooltip = "Meters below the hit blood reaches"});
    archive.Field("dripCount", dripCount, {.min = 0.0f, .max = 32.0f, .tooltip = "Drops fallen around each hit"});
    archive.Field("dripRadius", dripRadius, {.min = 0.0f, .max = 5.0f, .speed = 0.01f, .tooltip = "Meters"});
    archive.Field("minSize", minSize, {.min = 0.01f, .max = 5.0f, .speed = 0.01f});
    archive.Field("maxSize", maxSize, {.min = 0.01f, .max = 5.0f, .speed = 0.01f});
}

} // namespace Gameplay
