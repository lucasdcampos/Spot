#include "explosion.h"

#include "explosive_barrel.h"

#include <algorithm>

namespace Explosion {

void Explode(Spot::Scene& scene, Spot::Vector3 center, float radius, float impulse, Spot::EntityId ignore) {
    // The flash, until particles (M13) give explosions a look of their own.
    Spot::DebugDraw::Sphere(center, radius, {.color = {1.0f, 0.6f, 0.1f, 1.0f}, .duration = 0.3f});

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
