#pragma once

#include "textures.h"

#include <cstdint>

namespace Generator::Textures {

// The level's surfaces (grass, gravel, asphalt, paving, brick, corrugated steel, tread plate, rock, bark, leaves,
// water, concrete slabs and needles), size pixels square.
SurfaceTextures PaintLevelSurface(Surface surface, std::uint32_t size);

} // namespace Generator::Textures
