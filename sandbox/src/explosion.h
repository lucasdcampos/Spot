#pragma once

#include <spot.h>

// Explosions push every body within their radius away from their center, harder the closer it is, and set off the
// explosive barrels they reach. They hurt the players they reach, by up to the damage at the center and less behind
// cover, throw them back and shake the view of those around. They sound with the scene's SoundBank's explosion, light
// up what is around with a flickering flash, and burst into fire, smoke, embers and debris, raising dust from the
// ground close below. At water, they throw up a plume of it, and under it they make no fire.
namespace Explosion {

void Explode(Spot::Scene& scene, Spot::Vector3 center, float radius, float impulse, float damage,
             Spot::EntityId ignore = Spot::EntityId::Invalid);

} // namespace Explosion
