#pragma once

#include <spot.h>

// Explosions push every body within their radius away from their center, harder the closer it is, and set off the
// explosive barrels they reach.
namespace Explosion {

void Explode(Spot::Scene& scene, Spot::Vector3 center, float radius, float impulse,
             Spot::EntityId ignore = Spot::EntityId::Invalid);

} // namespace Explosion
