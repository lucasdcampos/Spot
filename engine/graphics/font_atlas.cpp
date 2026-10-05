#include "graphics/font_atlas.h"

#include "core/log.h"

#include <array>
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

// Sampling each glyph at twice the resolution keeps text smooth when it lands between pixels.
constexpr unsigned int Oversampling = 2;

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

    std::vector<std::vector<stbtt_packedchar>> packed(Ranges.size());
    std::vector<stbtt_pack_range> packRanges(Ranges.size());
    for (std::size_t i = 0; i < Ranges.size(); ++i) {
        packed[i].resize(Ranges[i].last - Ranges[i].first + 1);
        packRanges[i].font_size = pixelSize;
        packRanges[i].first_unicode_codepoint_in_range = static_cast<int>(Ranges[i].first);
        packRanges[i].num_chars = static_cast<int>(packed[i].size());
        packRanges[i].chardata_for_range = packed[i].data();
    }

    // Starts small and doubles until every glyph fits.
    int size = 256;
    std::vector<unsigned char> coverage;
    for (;; size *= 2) {
        if (size > MaxAtlasSize) {
            Log::GetEngineLogger().Error("Glyphs at {} pixels do not fit in a {}x{} atlas", pixelSize, MaxAtlasSize,
                                         MaxAtlasSize);
            return std::nullopt;
        }
        coverage.assign(static_cast<std::size_t>(size) * size, 0);
        stbtt_pack_context context;
        if (!stbtt_PackBegin(&context, coverage.data(), size, size, 0, 1, nullptr)) {
            return std::nullopt;
        }
        stbtt_PackSetOversampling(&context, Oversampling, Oversampling);
        const bool fits =
            stbtt_PackFontRanges(&context, data, 0, packRanges.data(), static_cast<int>(packRanges.size()));
        stbtt_PackEnd(&context);
        if (fits) {
            break;
        }
    }

    const float atlasSize = static_cast<float>(size);
    for (std::size_t i = 0; i < Ranges.size(); ++i) {
        for (std::size_t j = 0; j < packed[i].size(); ++j) {
            const stbtt_packedchar& c = packed[i][j];
            const char32_t codePoint = Ranges[i].first + static_cast<char32_t>(j);
            Glyph glyph;
            glyph.advance = c.xadvance;
            // Packing pads every glyph, so blank ones such as the space still get a box; they keep an empty one.
            if (stbtt_IsGlyphEmpty(&impl->info, stbtt_FindGlyphIndex(&impl->info, static_cast<int>(codePoint)))) {
                impl->glyphs.emplace(codePoint, glyph);
                continue;
            }
            // stb_truetype measures Y downward from the baseline.
            glyph.min = {c.xoff, -c.yoff2};
            glyph.max = {c.xoff2, -c.yoff};
            glyph.uvMin = {static_cast<float>(c.x0) / atlasSize, static_cast<float>(c.y0) / atlasSize};
            glyph.uvMax = {static_cast<float>(c.x1) / atlasSize, static_cast<float>(c.y1) / atlasSize};
            impl->glyphs.emplace(codePoint, glyph);
        }
    }

    std::vector<std::uint8_t> pixels(coverage.size() * 4);
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = coverage[i];
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
