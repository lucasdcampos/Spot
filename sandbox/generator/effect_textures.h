#pragma once

#include <spot.h>

// The particle effects' textures: white to grey shapes whose alpha covers them, which particles tint. Painted from
// noise with fixed seeds, using only arithmetic and square roots, so every machine paints the same pixels.
namespace Generator::EffectTextures {

// A billowing puff, lit from above: lighter on its top, shadowed in its folds. Explosions' smoke, dust and spray.
Spot::Image Smoke();
// A turbulent tongue of fire: bright filaments with dark gaps, ragged at its edges.
Spot::Image Fire();
// A muzzle flash seen from the front: a hot core and five uneven petals.
Spot::Image MuzzleFlash();
// A flame seen from the side, its length up the texture, for particles stretched along their velocity.
Spot::Image Flame();
// A jagged chunk of rubble, lit from above.
Spot::Image Debris();
// Rings of ripples spreading on water, the outermost the strongest, for flat particles.
Spot::Image Ripple();

} // namespace Generator::EffectTextures
