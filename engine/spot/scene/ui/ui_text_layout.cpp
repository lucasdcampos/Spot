#include "spot/scene/ui/ui_text_layout.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/archive.h"
#include "spot/core/utf8.h"
#include "spot/render/font_atlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Spot {

void UiTextLayoutSettings::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 3> Alignments{"Left", "Center", "Right"};
    static constexpr std::array<std::string_view, 3> Overflows{"Overflow", "Clip", "Ellipsis"};
    static constexpr std::array<std::string_view, 3> VerticalAlignments{"Top", "Middle", "Bottom"};
    archive.Field("alignment", alignment, {.enumNames = Alignments});
    archive.Field("overflow", overflow, {.enumNames = Overflows});
    archive.Field("wrap", wrap);
    archive.Field("verticalAlignment", verticalAlignment, {.enumNames = VerticalAlignments});
}

UiTextGeometry LayoutUiText(const FontAtlas& atlas, std::string_view text, Vector2 box,
                            const UiTextLayoutSettings& settings) {
    UiTextGeometry result;
    const auto points = DecodeUtf8(text);
    const float width = std::isfinite(box.x) ? std::max(0.0f, box.x) : 0;
    const float lineHeight = atlas.GetLineHeight();
    const float height = std::isfinite(box.y) ? std::max(0.0f, box.y) : 0;
    const int maxLines = settings.overflow == UiTextOverflow::Overflow
                             ? std::numeric_limits<int>::max()
                             : static_cast<int>(std::min(100000.0f, std::floor(height / lineHeight)));
    if (maxLines == 0)
        return result;
    struct Line {
        std::u32string points;
        float width = 0;
    };
    std::vector<Line> lines(1);
    auto advance = [&](char32_t previous, char32_t point) {
        const auto* glyph = atlas.FindGlyph(point == U'\t' ? U' ' : point);
        return (glyph ? glyph->advance * (point == U'\t' ? 4.0f : 1.0f) : 0) +
               (previous && point != U'\t' ? atlas.GetKerning(previous, point) : 0);
    };
    bool truncated = false;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto point = points[index];
        if (point == U'\r')
            continue;
        auto* line = &lines.back();
        if (settings.wrap && point != U' ' && point != U'\t' && point != U'\n' && !line->points.empty() &&
            (line->points.back() == U' ' || line->points.back() == U'\t')) {
            float wordWidth = 0;
            char32_t previous = line->points.back();
            for (std::size_t next = index;
                 next < points.size() && points[next] != U' ' && points[next] != U'\t' && points[next] != U'\n';
                 ++next) {
                wordWidth += advance(previous, points[next]);
                previous = points[next];
            }
            if (line->width + wordWidth > width) {
                if (static_cast<int>(lines.size()) >= maxLines) {
                    truncated = true;
                    break;
                }
                lines.emplace_back();
                line = &lines.back();
            }
        }
        float step = advance(line->points.empty() ? 0 : line->points.back(), point);
        if (point == U'\n' || (settings.wrap && !line->points.empty() && line->width + step > width)) {
            if (static_cast<int>(lines.size()) >= maxLines) {
                truncated = true;
                break;
            }
            lines.emplace_back();
            line = &lines.back();
            step = advance(0, point);
            if (point == U'\n' || point == U' ' || point == U'\t')
                continue;
        }
        if (!settings.wrap && settings.overflow == UiTextOverflow::Ellipsis && line->width + step > width) {
            truncated = true;
            break;
        }
        line->points.push_back(point);
        line->width += step;
    }
    if (truncated && settings.overflow == UiTextOverflow::Ellipsis) {
        auto& line = lines.back();
        const float dots = advance(0, U'.') * 3 + atlas.GetKerning(U'.', U'.') * 2;
        while (!line.points.empty() && line.width + dots > width) {
            const auto last = line.points.back();
            line.points.pop_back();
            line.width -= advance(line.points.empty() ? 0 : line.points.back(), last);
        }
        if (dots <= width) {
            line.points += U"...";
            line.width += dots;
        }
    }
    // The lines' boxes, from the ascent to the descent, are what is centered: for most fonts the middle of capitals
    // and digits is within a pixel of theirs.
    const float block = static_cast<float>(lines.size()) * lineHeight;
    const float top = settings.verticalAlignment == UiTextVerticalAlignment::Middle   ? (height - block) * 0.5f
                      : settings.verticalAlignment == UiTextVerticalAlignment::Bottom ? height - block
                                                                                      : 0;
    float baseline = -atlas.GetAscent() - top;
    for (const auto& line : lines) {
        float pen = settings.alignment == UiTextAlignment::Center  ? (width - line.width) * 0.5f
                    : settings.alignment == UiTextAlignment::Right ? width - line.width
                                                                   : 0;
        char32_t previous = 0;
        for (const auto point : line.points) {
            if (point == U'\t') {
                pen += advance(0, point);
                previous = 0;
                continue;
            }
            const auto* glyph = atlas.FindGlyph(point);
            if (!glyph)
                continue;
            if (previous)
                pen += atlas.GetKerning(previous, point);
            if (glyph->max.x > glyph->min.x && glyph->max.y > glyph->min.y) {
                const Vector2 origin{pen, baseline};
                result.glyphs.push_back({origin + glyph->min, origin + glyph->max, glyph->uvMin, glyph->uvMax});
            }
            pen += glyph->advance;
            previous = point;
        }
        result.size.x = std::max(result.size.x, line.width);
        baseline -= lineHeight;
    }
    result.size.y = static_cast<float>(lines.size()) * lineHeight;
    return result;
}

std::vector<UiImageQuad> BuildUiNineSlice(UiBounds bounds, Vector2 textureSize, Vector4 borders, float scale) {
    std::vector<UiImageQuad> quads;
    if (bounds.IsEmpty() || textureSize.x <= 0 || textureSize.y <= 0 || !std::isfinite(scale) || scale <= 0)
        return quads;
    auto positive = [](float value) { return std::isfinite(value) ? std::max(0.0f, value) : 0; };
    float left = positive(borders.x), bottom = positive(borders.y), right = positive(borders.z),
          top = positive(borders.w);
    auto fit = [](float& a, float& b, float size) {
        if (a + b > size) {
            const float ratio = size / (a + b);
            a *= ratio;
            b *= ratio;
        }
    };
    fit(left, right, textureSize.x);
    fit(bottom, top, textureSize.y);
    float drawLeft = left * scale, drawRight = right * scale, drawBottom = bottom * scale, drawTop = top * scale;
    fit(drawLeft, drawRight, bounds.GetSize().x);
    fit(drawBottom, drawTop, bounds.GetSize().y);
    const std::array xs{bounds.min.x, bounds.min.x + drawLeft, bounds.max.x - drawRight, bounds.max.x};
    const std::array ys{bounds.min.y, bounds.min.y + drawBottom, bounds.max.y - drawTop, bounds.max.y};
    const std::array us{0.0f, left / textureSize.x, 1.0f - right / textureSize.x, 1.0f};
    const std::array vs{1.0f, 1.0f - bottom / textureSize.y, top / textureSize.y, 0.0f};
    for (std::size_t y = 0; y < 3; ++y)
        for (std::size_t x = 0; x < 3; ++x) {
            UiBounds quad{{xs[x], ys[y]}, {xs[x + 1], ys[y + 1]}};
            if (!quad.IsEmpty())
                quads.push_back({quad, {{us[x], vs[y + 1]}, {us[x + 1], vs[y]}}});
        }
    return quads;
}

} // namespace Spot
