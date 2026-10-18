#include "explosion.h"

#include "explosive_barrel.h"
#include "flash.h"
#include "particle_effects.h"
#include "player_health.h"
#include "player_movement.h"
#include "robot.h"
#include "sound_bank.h"

#include <algorithm>

namespace Explosion {

namespace {

// Hurts and throws back the players within the radius.
void HurtPlayers(Spot::Scene& scene, Spot::Vector3 center, float radius, float impulse, float damage) {
    scene.ForEachComponent<PlayerHealth>([&](PlayerHealth& health) {
        Spot::Entity& player = health.GetEntity();
        const auto* body = player.GetComponent<Spot::CharacterController>();
        const float half = body ? body->GetHeight() * 0.5f : 0.9f;
        const float width = body ? body->GetRadius() : 0.4f;
        // The nearest point of the player's capsule.
        const Spot::Vector3 middle =
            player.GetTransform().GetWorldPosition() + (body ? body->GetCenter() : Spot::Vector3{});
        const Spot::Vector3 axis{middle.x, std::clamp(center.y, middle.y - half + width, middle.y + half - width),
                                 middle.z};
        const float toAxis = Spot::Distance(center, axis);
        const float distance = std::max(toAxis - width, 0.0f);
        if (radius <= 0.0f || distance >= radius) {
            return;
        }
        float share = 1.0f - distance / radius;
        const Spot::Vector3 direction = toAxis > 0.01f ? (axis - center) / toAxis : Spot::Vector3{0.0f, 1.0f, 0.0f};
        // From a little out of the center, past what exploded, to the player: anything between shields it.
        const float start = std::min(0.5f, distance);
        if (distance - start > 0.05f && scene.GetPhysics().Raycast(center + direction * start, direction,
                                                                   distance - start, {.ignore = player.GetId()})) {
            share *= 0.35f;
        }
        health.Damage(damage * share);
        if (auto* movement = player.GetComponent<PlayerMovement>()) {
            // As much as a body of a person's weight would get, lifted a little.
            const float speed = std::min(impulse / 70.0f * share, 14.0f);
            movement->Push(Spot::Normalize(direction + Spot::Vector3{0.0f, 0.6f, 0.0f}) * speed);
        }
    });
}

} // namespace

void Explode(Spot::Scene& scene, Spot::Vector3 center, float radius, float impulse, float damage,
             Spot::EntityId ignore) {
    ParticleEffects::Explosion(scene, center, radius);
    Robots::Explode(scene, center, radius, impulse);
    HurtPlayers(scene, center, radius, impulse, damage);
    // The light flash accompanies the fire, smoke and flying debris.
    Flash::Spawn(scene, center, {1.0f, 0.55f, 0.2f, 1.0f}, radius * 0.6f, radius * 4.0f, 20.0f, 0.35f);
    if (const SoundBank* bank = SoundBank::Find(scene); bank && bank->explosion) {
        // Heard from afar, and never the one sound dropped when voices run out.
        scene.GetAudio().PlayOneShot(bank->explosion.GetShared(), center,
                                     {.bus = "Effects",
                                      .spatialSettings = {.minDistance = 6.0f, .maxDistance = 150.0f},
                                      .priority = 10,
                                      .maxInstances = 4});
        Spot::Input::RumbleGamepad(0.8f, 0.6f, 0.3f);
    }

    for (const Spot::OverlapHit& hit : scene.GetPhysics().OverlapSphere(center, radius, {.ignore = ignore})) {
        Spot::Entity* entity = scene.GetEntity(hit.entity);
        if (!entity) {
            continue;
        }
        if (auto* barrel = entity->GetComponent<ExplosiveBarrel>()) {
            barrel->Ignite();
        }
        auto* body = entity->GetComponent<Spot::Rigidbody>();
        if (!body || body->IsKinematic()) {
            continue;
        }
        const Spot::Vector3 offset = entity->GetTransform().GetWorldPosition() - center;
        const float distance = Spot::Length(offset);
        // Straight up from right below the center, and harder the closer to it, down to nothing at the radius.
        const Spot::Vector3 direction = distance > 0.01f ? offset / distance : Spot::Vector3{0.0f, 1.0f, 0.0f};
        const float strength = impulse * std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
        // A little lift, so things fly rather than slide.
        body->AddImpulse(Spot::Normalize(direction + Spot::Vector3{0.0f, 0.3f, 0.0f}) * strength);
    }
}

} // namespace Explosion
