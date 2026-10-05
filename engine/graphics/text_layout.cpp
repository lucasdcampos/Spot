#include "graphics/text_layout.h"

#include "core/utf8.h"
#include "graphics/font_atlas.h"

#include <algorithm>
#include <functional>

namespace Spot {

namespace {

constexpr int SpacesPerTab = 4;

// Walks the text and reports where each glyph goes; both layout and measuring follow exactly the same rules.
void ForEachGlyph(const FontAtlas& atlas, std::string_view text,
                  const std::function<void(const Glyph&, Vector2 pen)>& visit, float& widestLine, int& lineCount) {
    const std::u32string codePoints = DecodeUtf8(text);
    const Glyph* space = atlas.FindGlyph(U' ');

    Vector2 pen{0.0f, -atlas.GetAscent()};
    char32_t previous = 0;
    widestLine = 0.0f;
    lineCount = 1;

    for (const char32_t codePoint : codePoints) {
        if (codePoint == U'\n') {
            widestLine = std::max(widestLine, pen.x);
            pen = {0.0f, pen.y - atlas.GetLineHeight()};
            previous = 0;
            ++lineCount;
            continue;
        }
        if (codePoint == U'\r') {
            continue;
        }
        if (codePoint == U'\t') {
            pen.x += space ? space->advance * SpacesPerTab : 0.0f;
            previous = 0;
            continue;
        }

        const Glyph* glyph = atlas.FindGlyph(codePoint);
        if (!glyph) {
            continue;
        }
        if (previous != 0) {
            pen.x += atlas.GetKerning(previous, codePoint);
        }
        visit(*glyph, pen);
        pen.x += glyph->advance;
        previous = codePoint;
    }
    widestLine = std::max(widestLine, pen.x);
}

} // namespace

std::vector<GlyphQuad> LayoutText(const FontAtlas& atlas, std::string_view text) {
    std::vector<GlyphQuad> quads;
    float widestLine = 0.0f;
    int lineCount = 0;
    ForEachGlyph(
        atlas, text,
        [&](const Glyph& glyph, Vector2 pen) {
            if (glyph.max.x > glyph.min.x && glyph.max.y > glyph.min.y) {
                quads.push_back({pen + glyph.min, pen + glyph.max, glyph.uvMin, glyph.uvMax});
            }
        },
        widestLine, lineCount);
    return quads;
}

Vector2 MeasureText(const FontAtlas& atlas, std::string_view text) {
    float widestLine = 0.0f;
    int lineCount = 0;
    ForEachGlyph(atlas, text, [](const Glyph&, Vector2) {}, widestLine, lineCount);
    return {widestLine, static_cast<float>(lineCount) * atlas.GetLineHeight()};
}

} // namespace Spot
