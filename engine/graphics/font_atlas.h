#pragma once

#include "graphics/image.h"
#include "math/vector.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace Spot {

struct Glyph {
    // The glyph's rectangle relative to the pen on the baseline, in pixels with Y up.
    Vector2 min;
    Vector2 max;
    // Where the glyph is in the atlas image, from (0, 0) at its top-left to (1, 1) at its bottom-right.
    Vector2 uvMin;
    Vector2 uvMax;
    // How far the pen moves after the glyph, in pixels.
    float advance = 0.0f;
};

// The glyphs of a TrueType font rasterized at one size into a single RGBA image: white, with coverage in alpha, so
// tinting the image colors the text. It covers ASCII and Latin-1, enough for most Western European languages. It
// only builds CPU data; Font puts it on the GPU.
class FontAtlas {
public:
    // Returns nothing, after logging why, when the data is not a font stb_truetype can read. Malformed font files can
    // still crash it, since stb_truetype does not validate them: load fonts from trusted sources, such as the game's
    // own assets.
    static std::optional<FontAtlas> Create(std::span<const std::byte> fontData, float pixelSize);

    ~FontAtlas();
    FontAtlas(FontAtlas&&) noexcept;
    FontAtlas& operator=(FontAtlas&&) noexcept;

    float GetPixelSize() const;
    // Distance from the baseline up to the top of the tallest glyphs.
    float GetAscent() const;
    // Distance from one baseline to the next.
    float GetLineHeight() const;

    // Falls back to '?' for characters the atlas does not cover, and returns null only if that is missing too.
    const Glyph* FindGlyph(char32_t codePoint) const;
    // Extra advance between two characters, usually negative, from the font's kerning table.
    float GetKerning(char32_t first, char32_t second) const;

    const Image& GetImage() const;

private:
    struct Impl;

    explicit FontAtlas(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
