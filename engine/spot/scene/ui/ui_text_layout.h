#pragma once

#include "spot/render/quad_batch.h"
#include "spot/render/text_layout.h"
#include "spot/scene/ui/ui_layout.h"

#include <string_view>
#include <vector>

namespace Spot {

class FontAtlas;

enum class UiTextAlignment { Left, Center, Right };
enum class UiTextOverflow { Overflow, Clip, Ellipsis };
// Where the lines sit in a box taller than they are; Middle centers them, as on a button or in a panel.
enum class UiTextVerticalAlignment { Top, Middle, Bottom };

struct UiTextLayoutSettings {
    UiTextAlignment alignment = UiTextAlignment::Left;
    UiTextOverflow overflow = UiTextOverflow::Clip;
    bool wrap = true;
    UiTextVerticalAlignment verticalAlignment = UiTextVerticalAlignment::Top;
    void Serialize(class Archive& archive);
    friend bool operator==(const UiTextLayoutSettings&, const UiTextLayoutSettings&) = default;
};

struct UiTextGeometry {
    std::vector<GlyphQuad> glyphs;
    Vector2 size;
};

// Size and geometry use atlas pixels; the renderer can scale them together.
UiTextGeometry LayoutUiText(const FontAtlas& atlas, std::string_view text, Vector2 box,
                            const UiTextLayoutSettings& settings = {});

struct UiImageQuad {
    UiBounds bounds;
    TextureRegion region;
};

// Borders are left, bottom, right, top, in source pixels. Oversized borders shrink proportionally.
std::vector<UiImageQuad> BuildUiNineSlice(UiBounds bounds, Vector2 textureSize, Vector4 borders, float scale = 1);

} // namespace Spot
