#pragma once

#include <spot.h>

// Explosions push every body within their radius away from their center, harder the closer it is, and set off the
// explosive barrels they reach. They hurt the players they reach, by up to the damage at the center and less behind
// cover, and throw them back. They sound with the scene's SoundBank's explosion.
namespace Explosion {

void Explode(Spot::Scene& scene, Spot::Vector3 center, float radius, float impulse, float damage,
             Spot::EntityId ignore = Spot::EntityId::Invalid);

} // namespace Explosion
