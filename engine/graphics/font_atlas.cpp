#include "graphics/font_atlas.h"

#include "core/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace Spot {

namespace {

struct CodePointRange {
    char32_t first;
    char32_t last;
};

// Printable ASCII and Latin-1, which hold the letters of most Western European languages.
constexpr std::array<CodePointRange, 2> Ranges{{{0x20, 0x7E}, {0xA0, 0xFF}}};

constexpr int SdfPadding = 6;
constexpr unsigned char SdfEdge = 128;
constexpr float SdfDistanceScale = 20.0f;

constexpr int MaxAtlasSize = 4096;

} // namespace

struct FontAtlas::Impl {
    // stb_truetype keeps pointing into the font data, so it lives as long as the atlas.
    std::vector<unsigned char> fontData;
    stbtt_fontinfo info{};
    float pixelSize = 0.0f;
    float scale = 0.0f;
    float ascent = 0.0f;
    float lineHeight = 0.0f;
    std::unordered_map<char32_t, Glyph> glyphs;
    std::optional<Image> image;
};

std::optional<FontAtlas> FontAtlas::Create(std::span<const std::byte> fontData, float pixelSize) {
    if (!std::isfinite(pixelSize) || pixelSize < 1.0f || pixelSize > 256.0f) {
        Log::GetEngineLogger().Error("Font size must be finite and between 1 and 256 pixels");
        return std::nullopt;
    }
    auto impl = std::make_unique<Impl>();
    impl->fontData.assign(reinterpret_cast<const unsigned char*>(fontData.data()),
                          reinterpret_cast<const unsigned char*>(fontData.data()) + fontData.size());
    impl->pixelSize = pixelSize;

    // An offset of -1 means the data is not a font; stb_truetype would read before the buffer if given it.
    const unsigned char* data = impl->fontData.data();
    const int offset = impl->fontData.size() >= 12 ? stbtt_GetFontOffsetForIndex(data, 0) : -1;
    if (offset < 0 || !stbtt_InitFont(&impl->info, data, offset)) {
        Log::GetEngineLogger().Error("Font data is not a TrueType or OpenType font stb_truetype can read");
        return std::nullopt;
    }

    impl->scale = stbtt_ScaleForPixelHeight(&impl->info, pixelSize);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&impl->info, &ascent, &descent, &lineGap);
    impl->ascent = static_cast<float>(ascent) * impl->scale;
    impl->lineHeight = static_cast<float>(ascent - descent + lineGap) * impl->scale;

    struct Raster {
        char32_t codePoint;
        Glyph glyph;
        int width = 0;
        int height = 0;
        int x = 0;
        int y = 0;
        std::vector<unsigned char> pixels = {};
    };
    std::vector<Raster> rasters;
    for (const auto range : Ranges) {
        for (char32_t codePoint = range.first; codePoint <= range.last; ++codePoint) {
            Raster raster{codePoint, {}};
            int advance = 0;
            int bearing = 0;
            stbtt_GetCodepointHMetrics(&impl->info, static_cast<int>(codePoint), &advance, &bearing);
            raster.glyph.advance = static_cast<float>(advance) * impl->scale;
            int xOffset = 0;
            int yOffset = 0;
            auto* sdf =
                stbtt_GetCodepointSDF(&impl->info, impl->scale, static_cast<int>(codePoint), SdfPadding, SdfEdge,
                                      SdfDistanceScale, &raster.width, &raster.height, &xOffset, &yOffset);
            if (sdf) {
                raster.pixels.assign(sdf, sdf + raster.width * raster.height);
                raster.glyph.min = {static_cast<float>(xOffset), -static_cast<float>(yOffset + raster.height)};
                raster.glyph.max = {static_cast<float>(xOffset + raster.width), -static_cast<float>(yOffset)};
                stbtt_FreeSDF(sdf, nullptr);
            }
            rasters.push_back(std::move(raster));
        }
    }
    int size = 256;
    for (;; size *= 2) {
        if (size > MaxAtlasSize) {
            Log::GetEngineLogger().Error("SDF glyphs do not fit in the font atlas");
            return std::nullopt;
        }
        int x = 1;
        int y = 1;
        int rowHeight = 0;
        bool fits = true;
        for (auto& raster : rasters) {
            if (raster.pixels.empty())
                continue;
            if (x + raster.width + 1 > size) {
                x = 1;
                y += rowHeight + 1;
                rowHeight = 0;
            }
            if (raster.width + 2 > size || y + raster.height + 1 > size) {
                fits = false;
                break;
            }
            raster.x = x;
            raster.y = y;
            x += raster.width + 1;
            rowHeight = std::max(rowHeight, raster.height);
        }
        if (fits)
            break;
    }
    const float atlasSize = static_cast<float>(size);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 0);
    for (auto& raster : rasters) {
        if (!raster.pixels.empty()) {
            raster.glyph.uvMin = {static_cast<float>(raster.x) / atlasSize, static_cast<float>(raster.y) / atlasSize};
            raster.glyph.uvMax = {static_cast<float>(raster.x + raster.width) / atlasSize,
                                  static_cast<float>(raster.y + raster.height) / atlasSize};
            for (int y = 0; y < raster.height; ++y) {
                for (int x = 0; x < raster.width; ++x) {
                    const auto pixel = static_cast<std::size_t>((raster.y + y) * size + raster.x + x) * 4;
                    pixels[pixel] = pixels[pixel + 1] = pixels[pixel + 2] = 255;
                    pixels[pixel + 3] = raster.pixels[static_cast<std::size_t>(y * raster.width + x)];
                }
            }
        }
        impl->glyphs.emplace(raster.codePoint, raster.glyph);
    }
    impl->image.emplace(static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), std::move(pixels));

    return FontAtlas(std::move(impl));
}

FontAtlas::FontAtlas(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

FontAtlas::~FontAtlas() = default;
FontAtlas::FontAtlas(FontAtlas&&) noexcept = default;
FontAtlas& FontAtlas::operator=(FontAtlas&&) noexcept = default;

float FontAtlas::GetPixelSize() const {
    return m_impl->pixelSize;
}

float FontAtlas::GetAscent() const {
    return m_impl->ascent;
}

float FontAtlas::GetLineHeight() const {
    return m_impl->lineHeight;
}

const Glyph* FontAtlas::FindGlyph(char32_t codePoint) const {
    if (const auto it = m_impl->glyphs.find(codePoint); it != m_impl->glyphs.end()) {
        return &it->second;
    }
    if (const auto it = m_impl->glyphs.find(U'?'); it != m_impl->glyphs.end()) {
        return &it->second;
    }
    return nullptr;
}

float FontAtlas::GetKerning(char32_t first, char32_t second) const {
    const int kerning = stbtt_GetCodepointKernAdvance(&m_impl->info, static_cast<int>(first), static_cast<int>(second));
    return static_cast<float>(kerning) * m_impl->scale;
}

const Image& FontAtlas::GetImage() const {
    return *m_impl->image;
}

} // namespace Spot
