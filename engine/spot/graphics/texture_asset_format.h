#pragma once

#include "spot/assets/asset_file.h"
#include "spot/graphics/image.h"
#include "spot/graphics/texture.h"

namespace Spot::Detail {

struct TextureFile {
    // The pixels; a placeholder when the texture is block-compressed.
    Image image;
    bool mipmaps = true;
    // Colors, read as linear light, or data read as it is.
    bool srgb = true;
    // Block-compressed (BC1 or BC3, sRGB or not), the texture is its levels instead, the first width by height, each
    // half the one before down to 1x1, made when it was imported.
    std::optional<TextureFormat> compressedFormat = std::nullopt;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::vector<std::uint8_t>> levels = {};
};

void Encode(BinaryWriter& writer, const TextureFile& texture);
std::optional<TextureFile> DecodeTexture(std::span<const std::byte> data);
// RGBA pixels from a level of BC1 or BC3 blocks, for GPUs that cannot sample them.
std::vector<std::uint8_t> DecompressBlocks(TextureFormat format, std::uint32_t width, std::uint32_t height,
                                           std::span<const std::uint8_t> blocks);

} // namespace Spot::Detail
