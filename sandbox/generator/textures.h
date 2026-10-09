#pragma once

#include <spot.h>

// The props' base color textures, painted from code with fixed seeds, so every run paints the same pixels. Round
// props' textures follow the layout of Generator::Shapes: the side in the top half, the ends as discs below.
namespace Generator::Textures {

inline constexpr std::uint32_t Size = 256;

Spot::Image Crate();
Spot::Image Barrel();
Spot::Image ExplosiveBarrel();
// Tiles in both directions, with the grain along X.
Spot::Image Plank();
// Tiles in both directions, brushed along X, with rivets along the top and bottom.
Spot::Image Beam();
// For a sphere's UVs: colored slices around it, white at the poles.
Spot::Image Ball();
Spot::Image Cone();
// Tiles in both directions.
Spot::Image Concrete();
Spot::Image Wheel();

} // namespace Generator::Textures
