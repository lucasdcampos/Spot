#include "gameplay/health/fall_damage.h"

#include "gameplay/health/health.h"

namespace Gameplay {

SPOT_COMPONENT(FallDamage);

float FallDamage::Land(float speed) {
    Health* health = GetEntity().GetComponent<Health>();
    if (!health || !IsActiveAndEnabled() || speed <= safeFallSpeed || lethalFallSpeed <= safeFallSpeed) {
        return 0.0f;
    }
    return health->TakeDamage(
        {.amount = health->maxHealth * (speed - safeFallSpeed) / (lethalFallSpeed - safeFallSpeed),
         .type = damageType,
         .point = GetEntity().GetTransform().GetWorldPosition(),
         .instigator = GetEntity().GetId(),
         .source = GetEntity().GetId()});
}

void FallDamage::Serialize(Spot::Archive& archive) {
    archive.Field("safeFallSpeed", safeFallSpeed,
                  {.min = 0.0f, .max = 100.0f, .speed = 0.1f, .tooltip = "Landing faster, in m/s, hurts"});
    archive.Field("lethalFallSpeed", lethalFallSpeed,
                  {.min = 0.0f, .max = 200.0f, .speed = 0.1f, .tooltip = "Landing this fast, in m/s, kills"});
    archive.Field("damageType", damageType);
}

} // namespace Gameplay
