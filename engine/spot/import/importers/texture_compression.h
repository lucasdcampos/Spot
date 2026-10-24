#pragma once

#include "spot/assets/asset_formats.h"
#include "spot/graphics/image.h"
#include "spot/graphics/texture_asset_format.h"

namespace Spot::Detail {

// Whether the texture can be stored block-compressed: textures with mipmaps, which are seen at a distance and are
// most of a game's memory, and whose sides are whole blocks. Others, such as pixel art, keep their exact pixels.
bool CanCompressTexture(const Image& image, bool mipmaps);
// The image as BC1, or BC3 when any pixel is not opaque, with every mip level down to 1x1, made here from the image
// as light for colors. A quarter or an eighth of the memory of the pixels. With keepAlphaCoverage, each smaller level
// has as much of it at half alpha or more as the image, so cutouts at that cutoff stay as dense from afar.
TextureFile CompressTexture(const Image& image, bool srgb, bool keepAlphaCoverage = false);

} // namespace Spot::Detail
