#include "import/importers/texture_compression.h"

#include <algorithm>
#include <array>
#include <cmath>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include <stb_dxt.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Spot::Detail {

namespace {

float ToLinear(std::uint8_t value) {
    const float c = static_cast<float>(value) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t ToByte(float value, bool srgb) {
    float c = std::clamp(value, 0.0f, 1.0f);
    if (srgb) {
        c = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }
    return static_cast<std::uint8_t>(std::lround(c * 255.0f));
}

// Half the size, each pixel the average of the four below it, averaged as light for colors.
std::vector<std::uint8_t> Downsample(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height,
                                     bool srgb) {
    const std::uint32_t halfWidth = std::max(width / 2, 1u);
    const std::uint32_t halfHeight = std::max(height / 2, 1u);
    std::vector<std::uint8_t> result(static_cast<std::size_t>(halfWidth) * halfHeight * 4);
    for (std::uint32_t y = 0; y < halfHeight; ++y) {
        for (std::uint32_t x = 0; x < halfWidth; ++x) {
            std::array<float, 4> sum{};
            for (std::uint32_t dy = 0; dy < 2; ++dy) {
                for (std::uint32_t dx = 0; dx < 2; ++dx) {
                    const std::uint32_t sx = std::min(x * 2 + dx, width - 1);
                    const std::uint32_t sy = std::min(y * 2 + dy, height - 1);
                    const std::uint8_t* p = &pixels[(static_cast<std::size_t>(sy) * width + sx) * 4];
                    for (std::size_t c = 0; c < 4; ++c) {
                        sum[c] += srgb && c < 3 ? ToLinear(p[c]) : static_cast<float>(p[c]) / 255.0f;
                    }
                }
            }
            std::uint8_t* out = &result[(static_cast<std::size_t>(y) * halfWidth + x) * 4];
            for (std::size_t c = 0; c < 4; ++c) {
                out[c] = ToByte(sum[c] * 0.25f, srgb && c < 3);
            }
        }
    }
    return result;
}

// The level in 4x4 blocks, those past the edges of levels smaller than a block repeating the edge's pixels.
std::vector<std::uint8_t> CompressLevel(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height,
                                        bool alpha) {
    const std::uint32_t blocksX = (width + 3) / 4;
    const std::uint32_t blocksY = (height + 3) / 4;
    const std::size_t blockSize = alpha ? 16 : 8;
    std::vector<std::uint8_t> result(static_cast<std::size_t>(blocksX) * blocksY * blockSize);
    std::array<std::uint8_t, 64> block{};
    for (std::uint32_t by = 0; by < blocksY; ++by) {
        for (std::uint32_t bx = 0; bx < blocksX; ++bx) {
            for (std::uint32_t y = 0; y < 4; ++y) {
                for (std::uint32_t x = 0; x < 4; ++x) {
                    const std::uint32_t sx = std::min(bx * 4 + x, width - 1);
                    const std::uint32_t sy = std::min(by * 4 + y, height - 1);
                    std::copy_n(&pixels[(static_cast<std::size_t>(sy) * width + sx) * 4], 4, &block[(y * 4 + x) * 4]);
                }
            }
            stb_compress_dxt_block(&result[(static_cast<std::size_t>(by) * blocksX + bx) * blockSize], block.data(),
                                   alpha ? 1 : 0, STB_DXT_HIGHQUAL);
        }
    }
    return result;
}

// The share of the pixels whose alpha is at least the cutoff, as cutout surfaces draw them.
float GetCoverage(std::span<const std::uint8_t> pixels, float cutoff, float scale) {
    std::size_t covered = 0;
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        covered += static_cast<float>(pixels[i]) * scale >= cutoff * 255.0f;
    }
    return static_cast<float>(covered) / static_cast<float>(std::max<std::size_t>(pixels.size() / 4, 1));
}

// Averaging alpha thins cutouts out in smaller levels, as gaps blend into what they surround and sink below the
// cutoff; scaling the level's alpha so as much of it is covered as in the full-size image keeps leaves and grates as
// dense from afar (Castaño, "Computing Alpha Mipmaps"). The pixels are lifted a little past the cutoff, so the
// blocks' alpha, a few levels off, does not drop them below it again.
std::vector<std::uint8_t> KeepCoverage(std::vector<std::uint8_t> pixels, float coverage, float cutoff) {
    constexpr float Margin = 16.0f / 255.0f;
    float low = 0.0f;
    float high = 4.0f;
    for (int step = 0; step < 12; ++step) {
        const float middle = (low + high) * 0.5f;
        (GetCoverage(pixels, cutoff + Margin, middle) < coverage ? low : high) = middle;
    }
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = static_cast<std::uint8_t>(std::min(std::lround(static_cast<float>(pixels[i]) * high), 255l));
    }
    return pixels;
}

} // namespace

bool CanCompressTexture(const Image& image, bool mipmaps) {
    return mipmaps && image.GetWidth() % 4 == 0 && image.GetHeight() % 4 == 0 && image.GetWidth() > 0;
}

TextureFile CompressTexture(const Image& image, bool srgb, bool keepAlphaCoverage) {
    const std::span<const std::uint8_t> pixels = image.GetPixels();
    bool alpha = false;
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 255) {
            alpha = true;
            break;
        }
    }
    TextureFile file{.image = Image(1, 1, {0, 0, 0, 0}), .mipmaps = true, .srgb = srgb};
    file.compressedFormat = alpha ? (srgb ? TextureFormat::Bc3Srgb : TextureFormat::Bc3)
                                  : (srgb ? TextureFormat::Bc1Srgb : TextureFormat::Bc1);
    file.width = image.GetWidth();
    file.height = image.GetHeight();
    std::vector<std::uint8_t> level(pixels.begin(), pixels.end());
    std::uint32_t width = file.width;
    std::uint32_t height = file.height;
    constexpr float Cutoff = 0.5f;
    const float coverage = GetCoverage(pixels, Cutoff, 1.0f);
    while (true) {
        if (alpha && keepAlphaCoverage && file.levels.size() > 0) {
            file.levels.push_back(CompressLevel(KeepCoverage(level, coverage, Cutoff), width, height, alpha));
        } else {
            file.levels.push_back(CompressLevel(level, width, height, alpha));
        }
        if (width == 1 && height == 1) {
            break;
        }
        level = Downsample(level, width, height, srgb);
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
    }
    return file;
}

} // namespace Spot::Detail
