#include "gameplay/health/impact_damage.h"

#include "gameplay/health/damage.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(ImpactDamage);

void ImpactDamage::OnCollisionEnter(const Spot::Collision& collision) {
    const float speed = std::max(-Spot::Dot(collision.relativeVelocity, collision.normal), 0.0f);
    const Spot::Entity* other = GetScene().GetEntity(collision.other);
    if (!other || speed < minSpeed || other->IsDescendantOf(GetEntity())) {
        return;
    }
    Damage::Apply(GetScene(), collision.other,
                  {.amount = speed * damagePerSpeed,
                   .type = damageType,
                   .point = collision.pointCount > 0 ? collision.points[0] : other->GetTransform().GetWorldPosition(),
                   .impulse = -collision.normal * std::max(collision.impulse, speed * impulsePerSpeed),
                   .source = GetEntity().GetId(),
                   .pushesBody = false});
}

void ImpactDamage::Serialize(Spot::Archive& archive) {
    archive.Field("minSpeed", minSpeed,
                  {.min = 0.0f, .max = 100.0f, .speed = 0.05f, .tooltip = "Meters per second along the contact"});
    archive.Field("damagePerSpeed", damagePerSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.1f});
    archive.Field("impulsePerSpeed", impulsePerSpeed,
                  {.min = 0.0f, .max = 10000.0f, .speed = 1.0f, .tooltip = "Newton seconds of push at least"});
    archive.Field("damageType", damageType);
}

} // namespace Gameplay
