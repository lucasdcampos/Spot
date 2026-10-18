#pragma once

#include <spot.h>

// The HUD's icons, white silhouettes with smooth edges in their alpha, for tinting. The held items are seen from the
// side, muzzle to the right, on 256 by 128 pixels; the clock's sun and moon and the health cross are 128 pixels
// square. Shapes are signed distances in pixels, made only of arithmetic and square roots, so every machine paints
// the same pixels.
namespace Generator::Icons {

Spot::Image PhysicsGun();
Spot::Image Pistol();
Spot::Image Rifle();
Spot::Image Shotgun();
Spot::Image RocketLauncher();
Spot::Image ToolGun();
Spot::Image Sun();
Spot::Image Moon();
Spot::Image Health();
// One of the weapon wheel's eight sectors, pointing up: a ring's eighth between radii of 118 and 252 pixels from a
// center 4 pixels below the image's bottom edge, with a gap of 6 pixels to its neighbors, on 256 pixels square.
Spot::Image WheelSector();
// Clear in the middle, darkening toward the edges and the corners, to stretch over the screen: as the player is hurt,
// tinted red, and behind the weapon wheel.
Spot::Image Vignette();

} // namespace Generator::Icons
