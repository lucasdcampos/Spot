#pragma once

#include "assets/asset_formats.h"
#include "graphics/image.h"

namespace Spot::Detail {

// Whether the texture can be stored block-compressed: textures with mipmaps, which are seen at a distance and are
// most of a game's memory, and whose sides are whole blocks. Others, such as pixel art, keep their exact pixels.
bool CanCompressTexture(const Image& image, bool mipmaps);
// The image as BC1, or BC3 when any pixel is not opaque, with every mip level down to 1x1, made here from the image
// as light for colors. A quarter or an eighth of the memory of the pixels.
TextureFile CompressTexture(const Image& image, bool srgb);

} // namespace Spot::Detail
