#pragma once

#include "textures.h"

#include <cstdint>

// Surfaces painted blade by blade and leaf by leaf, size pixels square, rather than pixel by pixel.
namespace Generator::Textures {

// A lawn seen from above, to repeat every few meters: tufts of blades leaning out from their centers and blades on
// their own, of many greens with a few dry ones, over a mat of dead straw and dark soil. Lower blades are darker and
// less lit, and the blades' edges stand out in the normals.
SurfaceTextures PaintGrass(std::uint32_t size);

// A twig of oak leaves, alternating along it and two side shoots, for the cards a broadleaf tree's canopy is made of.
// It grows up from the bottom middle; around it the texture is transparent, colored as the leaves so mipmaps do not
// darken their edges. Leaves painted later lie over the earlier ones, which are shaded by them.
SurfaceTextures PaintLeaves(std::uint32_t size);

// A flat spray of needles on a twig and its side shoots, as a spruce's, for the cards of conifers' branches: it grows
// up from the bottom middle, its shoots shorter toward its tip and their ends in the paler green of new growth.
// Around it the texture is transparent, colored as the needles.
SurfaceTextures PaintNeedles(std::uint32_t size);

} // namespace Generator::Textures
