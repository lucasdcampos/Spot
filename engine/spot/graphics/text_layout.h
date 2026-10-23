#pragma once

#include "spot/math/vector.h"

#include <string_view>
#include <vector>

namespace Spot {

class FontAtlas;

// One glyph of laid-out text, positioned relative to the text's top-left corner, in pixels with Y up: the text
// extends toward negative Y.
struct GlyphQuad {
    Vector2 min;
    Vector2 max;
    Vector2 uvMin;
    Vector2 uvMax;
};

// Places UTF-8 text glyph by glyph, applying kerning. '\n' starts a new line and a tab advances four spaces.
// Characters without a visible shape, such as spaces, only move the pen.
std::vector<GlyphQuad> LayoutText(const FontAtlas& atlas, std::string_view text);

// The size of the text's box: the widest line, by the number of lines times the line height.
Vector2 MeasureText(const FontAtlas& atlas, std::string_view text);

} // namespace Spot
