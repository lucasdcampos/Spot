#pragma once

#include "textures.h"

#include <cstdint>

// The props' own PBR textures, painted for their shapes, so their relief, roughness and metal follow what is painted:
// a crate's frame stands out from its planks and its nails catch the light. Round props' textures follow the layout of
// Generator::Shapes: the side in the top half, the ends as discs below.
namespace Generator::Textures {

inline constexpr std::uint32_t PropSize = 1024;

// One face of a crate: planks inside a frame, braced across, and nailed.
SurfaceTextures Crate(std::uint32_t size = PropSize);
// Steel drums, painted, worn where they roll and stand: one blue with a paper label, one red with hazard markings.
SurfaceTextures Barrel(std::uint32_t size = PropSize);
SurfaceTextures ExplosiveBarrel(std::uint32_t size = PropSize);
// Pine with two knots, tiling in both directions with the grain along X.
SurfaceTextures Plank(std::uint32_t size = PropSize);
// Painted steel rusting through, tiling along X, with rivets along the top and bottom of the beam's faces, the
// texture's top 0.27 of its height.
SurfaceTextures Beam(std::uint32_t size = PropSize);
// For a sphere's UVs: colored panels of vinyl around it, white at the poles.
SurfaceTextures Ball(std::uint32_t size = PropSize);
// A traffic cone with two reflective bands, its black base plate in the bottom-left quarter.
SurfaceTextures Cone(std::uint32_t size = PropSize);
// The tire's tread around its side, its sidewalls and steel hub on its ends.
SurfaceTextures Wheel(std::uint32_t size = PropSize);
// A paper bullseye stapled to plywood.
SurfaceTextures TargetDummy(std::uint32_t size = PropSize);

} // namespace Generator::Textures
