#include "explosion.h"

#include "explosive_barrel.h"
#include "flash.h"
#include "particle_effects.h"
#include "robot.h"
#include "sound_bank.h"

#include <algorithm>

namespace Explosion {

void Explode(Spot::Scene& scene, Spot::Vector3 center, float radius, float impulse, Spot::EntityId ignore) {
    ParticleEffects::Explosion(scene, center, radius);
    Robots::Explode(scene, center, radius, impulse);
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
