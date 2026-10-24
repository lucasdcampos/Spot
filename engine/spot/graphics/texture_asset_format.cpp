#include "spot/graphics/texture_asset_format.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace Spot::Detail {

namespace {

// Arrays of plain values are stored as a 64-bit count followed by the values.
template <typename T>
void WriteArray(BinaryWriter& writer, std::span<const T> values) {
    writer.Write(static_cast<std::uint64_t>(values.size()));
    writer.WriteBytes(std::as_bytes(values));
}

template <typename T>
std::optional<std::vector<T>> ReadArray(BinaryReader& reader) {
    const auto count = reader.Read<std::uint64_t>();
    if (!reader.IsValid() || count > (std::numeric_limits<std::size_t>::max)() / sizeof(T)) {
        return std::nullopt;
    }
    const auto bytes = reader.ReadView(static_cast<std::size_t>(count) * sizeof(T));
    if (!reader.IsValid()) {
        return std::nullopt;
    }
    std::vector<T> values(static_cast<std::size_t>(count));
    if (!values.empty()) {
        std::memcpy(values.data(), bytes.data(), bytes.size());
    }
    return values;
}

// The data must be read in full: leftover bytes mean it was written in another layout.
template <typename T>
std::optional<T> Finish(const BinaryReader& reader, T value) {
    if (!reader.IsValid() || !reader.IsAtEnd()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

void Encode(BinaryWriter& writer, const TextureFile& texture) {
    const bool compressed = texture.compressedFormat.has_value();
    writer.Write(compressed ? texture.width : texture.image.GetWidth());
    writer.Write(compressed ? texture.height : texture.image.GetHeight());
    writer.Write(static_cast<std::uint8_t>(texture.mipmaps));
    writer.Write(static_cast<std::uint8_t>(texture.srgb));
    writer.Write(static_cast<std::uint8_t>(compressed));
    if (compressed) {
        writer.Write(static_cast<std::uint32_t>(*texture.compressedFormat));
        writer.Write(static_cast<std::uint32_t>(texture.levels.size()));
        for (const auto& level : texture.levels) {
            WriteArray(writer, std::span<const std::uint8_t>(level));
        }
    } else {
        WriteArray(writer, texture.image.GetPixels());
    }
}

std::vector<std::uint8_t> DecompressBlocks(TextureFormat format, std::uint32_t width, std::uint32_t height,
                                           std::span<const std::uint8_t> blocks) {
    const bool alpha = format == TextureFormat::Bc3 || format == TextureFormat::Bc3Srgb;
    const std::size_t blockSize = alpha ? 16 : 8;
    const std::uint32_t blocksX = (width + 3) / 4;
    const std::uint32_t blocksY = (height + 3) / 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    if (blocks.size() < static_cast<std::size_t>(blocksX) * blocksY * blockSize) {
        return pixels;
    }
    const auto color565 = [](std::uint16_t c) {
        return std::array<int, 3>{((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31};
    };
    for (std::uint32_t by = 0; by < blocksY; ++by) {
        for (std::uint32_t bx = 0; bx < blocksX; ++bx) {
            const std::uint8_t* block = &blocks[(static_cast<std::size_t>(by) * blocksX + bx) * blockSize];
            std::array<std::uint8_t, 16> alphas{};
            alphas.fill(255);
            if (alpha) {
                // BC3's alpha: two endpoints and 3-bit indices between them.
                const int a0 = block[0];
                const int a1 = block[1];
                std::array<int, 8> table{a0, a1};
                for (int i = 0; i < 6; ++i) {
                    table[static_cast<std::size_t>(i + 2)] =
                        a0 > a1 ? ((6 - i) * a0 + (1 + i) * a1) / 7
                                : (i < 4 ? ((4 - i) * a0 + (1 + i) * a1) / 5 : (i == 4 ? 0 : 255));
                }
                std::uint64_t bits = 0;
                for (int i = 0; i < 6; ++i) {
                    bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
                }
                for (std::size_t i = 0; i < 16; ++i) {
                    alphas[i] = static_cast<std::uint8_t>(table[(bits >> (3 * i)) & 7]);
                }
                block += 8;
            }
            const auto c0 = static_cast<std::uint16_t>(block[0] | (block[1] << 8));
            const auto c1 = static_cast<std::uint16_t>(block[2] | (block[3] << 8));
            const auto e0 = color565(c0);
            const auto e1 = color565(c1);
            std::array<std::array<int, 4>, 4> colors{};
            colors[0] = {e0[0], e0[1], e0[2], 255};
            colors[1] = {e1[0], e1[1], e1[2], 255};
            // BC3 always interpolates; BC1 has a transparent black when the first endpoint is not the larger.
            if (alpha || c0 > c1) {
                for (std::size_t c = 0; c < 3; ++c) {
                    colors[2][c] = (2 * e0[c] + e1[c]) / 3;
                    colors[3][c] = (e0[c] + 2 * e1[c]) / 3;
                }
                colors[2][3] = colors[3][3] = 255;
            } else {
                for (std::size_t c = 0; c < 3; ++c) {
                    colors[2][c] = (e0[c] + e1[c]) / 2;
                }
                colors[2][3] = 255;
                colors[3] = {0, 0, 0, 0};
            }
            const std::uint32_t indices =
                block[4] | (block[5] << 8) | (block[6] << 16) | (static_cast<std::uint32_t>(block[7]) << 24);
            for (std::uint32_t y = 0; y < 4; ++y) {
                for (std::uint32_t x = 0; x < 4; ++x) {
                    const std::uint32_t px = bx * 4 + x;
                    const std::uint32_t py = by * 4 + y;
                    if (px >= width || py >= height) {
                        continue;
                    }
                    const std::size_t i = y * 4 + x;
                    const auto& color = colors[(indices >> (2 * i)) & 3];
                    std::uint8_t* out = &pixels[(static_cast<std::size_t>(py) * width + px) * 4];
                    out[0] = static_cast<std::uint8_t>(color[0]);
                    out[1] = static_cast<std::uint8_t>(color[1]);
                    out[2] = static_cast<std::uint8_t>(color[2]);
                    out[3] = alpha ? alphas[i] : static_cast<std::uint8_t>(color[3]);
                }
            }
        }
    }
    return pixels;
}

std::optional<TextureFile> DecodeTexture(std::span<const std::byte> data) {
    BinaryReader reader(data);
    const auto width = reader.Read<std::uint32_t>();
    const auto height = reader.Read<std::uint32_t>();
    const bool mipmaps = reader.Read<std::uint8_t>() != 0;
    const bool srgb = reader.Read<std::uint8_t>() != 0;
    if (reader.Read<std::uint8_t>() != 0) {
        const auto format = static_cast<TextureFormat>(reader.Read<std::uint32_t>());
        const auto levelCount = reader.Read<std::uint32_t>();
        const bool known = format == TextureFormat::Bc1 || format == TextureFormat::Bc1Srgb ||
                           format == TextureFormat::Bc3 || format == TextureFormat::Bc3Srgb;
        if (!reader.IsValid() || !known || levelCount == 0 || levelCount > 32 || width == 0 || height == 0) {
            return std::nullopt;
        }
        TextureFile file{.image = Image(1, 1, {0, 0, 0, 0}),
                         .mipmaps = mipmaps,
                         .srgb = srgb,
                         .compressedFormat = format,
                         .width = width,
                         .height = height};
        for (std::uint32_t i = 0; i < levelCount; ++i) {
            auto level = ReadArray<std::uint8_t>(reader);
            if (!level ||
                level->size() != GetTextureDataSize(format, std::max(width >> i, 1u), std::max(height >> i, 1u))) {
                return std::nullopt;
            }
            file.levels.push_back(std::move(*level));
        }
        return Finish(reader, std::move(file));
    }
    auto pixels = ReadArray<std::uint8_t>(reader);
    if (!pixels || pixels->size() != static_cast<std::size_t>(width) * height * 4) {
        return std::nullopt;
    }
    return Finish(reader, TextureFile{Image(width, height, std::move(*pixels)), mipmaps, srgb});
}

} // namespace Spot::Detail
