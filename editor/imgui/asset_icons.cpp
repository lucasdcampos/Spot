#include "imgui/asset_icons.h"

#include "code/script_editor.h"
#include "imgui/editor_style.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <span>
#include <string>

namespace Spot::Editor::AssetIcons {

namespace {

ImVec2 operator+(ImVec2 a, ImVec2 b) {
    return {a.x + b.x, a.y + b.y};
}

ImVec2 operator-(ImVec2 a, ImVec2 b) {
    return {a.x - b.x, a.y - b.y};
}

ImVec2 operator*(ImVec2 a, float k) {
    return {a.x * k, a.y * k};
}

ImVec2 Lerp(ImVec2 a, ImVec2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

ImVec4 Lerp(ImVec4 a, ImVec4 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

constexpr ImVec4 Rgb(int r, int g, int b) {
    return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, 1.0f};
}

ImVec4 Lighten(ImVec4 c, float t) {
    return {c.x + (1.0f - c.x) * t, c.y + (1.0f - c.y) * t, c.z + (1.0f - c.z) * t, c.w};
}

ImVec4 Darken(ImVec4 c, float f) {
    return {c.x * f, c.y * f, c.z * f, c.w};
}

ImVec4 WithAlpha(ImVec4 c, float a) {
    return {c.x, c.y, c.z, a};
}

ImU32 ToU32(ImVec4 c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

constexpr ImVec4 TextureAccent = Rgb(86, 178, 132);
constexpr ImVec4 MeshAccent = Rgb(78, 160, 222);
constexpr ImVec4 MaterialAccent = Rgb(214, 108, 104);
constexpr ImVec4 ModelAccent = Rgb(226, 146, 82);
constexpr ImVec4 SoundAccent = Rgb(214, 102, 152);
constexpr ImVec4 FontAccent = Rgb(64, 178, 162);
constexpr ImVec4 SceneAccent = Rgb(92, 146, 230);
constexpr ImVec4 SourceAccent = Rgb(146, 104, 214);
constexpr ImVec4 HeaderAccent = Rgb(110, 128, 220);
constexpr ImVec4 BuildScriptAccent = Rgb(98, 170, 96);
constexpr ImVec4 FileAccent = Rgb(128, 134, 146);

// Paper for documents and the photo mat: a cool light gray, not white, so it does not glare on the dark panel.
constexpr ImVec4 PaperTop = Rgb(214, 218, 225);
constexpr ImVec4 PaperBottom = Rgb(166, 172, 183);
constexpr ImVec4 Ink = Rgb(112, 119, 132);
// Neutral clay, like the material the model thumbnails are drawn with.
constexpr ImVec4 Clay = Rgb(176, 178, 186);
constexpr ImU32 White = IM_COL32_WHITE;

// Recolors the vertices added since vertexStart along a linear gradient from (p0, c0) to (p1, c1). Shapes are drawn
// opaque white first, so each vertex's alpha is its anti-aliasing coverage, which is kept.
void ApplyGradient(ImDrawList* drawList, int vertexStart, ImVec2 p0, ImVec2 p1, ImVec4 c0, ImVec4 c1) {
    const ImVec2 d = p1 - p0;
    const float inverseLengthSquared = 1.0f / std::max(d.x * d.x + d.y * d.y, 1e-6f);
    for (int i = vertexStart; i < drawList->VtxBuffer.Size; ++i) {
        ImDrawVert& vertex = drawList->VtxBuffer[i];
        const ImVec2 offset = vertex.pos - p0;
        const float t = std::clamp((offset.x * d.x + offset.y * d.y) * inverseLengthSquared, 0.0f, 1.0f);
        ImVec4 color = Lerp(c0, c1, t);
        color.w *= static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        vertex.col = ToU32(color);
    }
}

// Fills the current path, clockwise and convex, with a linear gradient from g0 to g1.
void FillPath(ImDrawList* drawList, ImVec2 g0, ImVec2 g1, ImVec4 c0, ImVec4 c1) {
    const int start = drawList->VtxBuffer.Size;
    drawList->PathFillConvex(White);
    ApplyGradient(drawList, start, g0, g1, c0, c1);
}

// Fills a rounded rectangle with a vertical gradient.
void Rect(ImDrawList* drawList, ImVec2 a, ImVec2 b, float rounding, ImVec4 top, ImVec4 bottom,
          ImDrawFlags flags = ImDrawFlags_None) {
    const int start = drawList->VtxBuffer.Size;
    drawList->AddRectFilled(a, b, White, rounding, flags);
    ApplyGradient(drawList, start, a, {a.x, b.y}, top, bottom);
}

// Fills a convex polygon, of either winding, with a linear gradient from g0 to g1.
void Poly(ImDrawList* drawList, std::initializer_list<ImVec2> points, ImVec2 g0, ImVec2 g1, ImVec4 c0, ImVec4 c1) {
    const std::span<const ImVec2> span(points.begin(), points.size());
    // ImGui's anti-aliased fill wants clockwise points on screen, where y goes down: a positive signed area.
    float area = 0.0f;
    for (std::size_t i = 0; i < span.size(); ++i) {
        const ImVec2 p = span[i];
        const ImVec2 q = span[(i + 1) % span.size()];
        area += p.x * q.y - q.x * p.y;
    }
    drawList->PathClear();
    for (std::size_t i = 0; i < span.size(); ++i) {
        drawList->PathLineTo(span[area >= 0.0f ? i : span.size() - 1 - i]);
    }
    FillPath(drawList, g0, g1, c0, c1);
}

void HorizontalLine(ImDrawList* drawList, float x0, float x1, float y, ImVec4 color) {
    drawList->AddLine({x0, y}, {x1, y}, ToU32(color), 1.0f);
}

// A soft drop shadow: rounded rectangles stacked from inset to outset at a low alpha blur it by the width.
void SoftShadow(ImDrawList* drawList, ImVec2 a, ImVec2 b, float rounding, float blur, float alpha) {
    constexpr int Layers = 6;
    const ImU32 color = ToU32({0.0f, 0.0f, 0.0f, alpha / Layers * 1.6f});
    for (int i = 0; i < Layers; ++i) {
        const float grow = blur * (static_cast<float>(i + 1) / Layers - 0.5f);
        const ImVec2 low{a.x - grow, a.y - grow};
        const ImVec2 high{b.x + grow, b.y + grow};
        if (high.x > low.x && high.y > low.y) {
            drawList->AddRectFilled(low, high, color, std::max(0.0f, rounding + grow));
        }
    }
}

// A soft elliptical contact shadow: shrinking stacked ellipses darken toward the middle.
void EllipseShadow(ImDrawList* drawList, ImVec2 center, ImVec2 radius, float alpha) {
    constexpr int Layers = 5;
    constexpr int Segments = 40;
    std::array<ImVec2, Segments> points;
    const ImU32 color = ToU32({0.0f, 0.0f, 0.0f, alpha / Layers * 1.5f});
    for (int layer = 0; layer < Layers; ++layer) {
        const float scale = 1.0f - static_cast<float>(layer) * 0.17f;
        for (int i = 0; i < Segments; ++i) {
            const float angle = static_cast<float>(i) * 2.0f * std::numbers::pi_v<float> / Segments;
            points[i] = {center.x + std::cos(angle) * radius.x * scale, center.y + std::sin(angle) * radius.y * scale};
        }
        drawList->AddConvexPolyFilled(points.data(), Segments, color);
    }
}

// Draws a short word centered on the point, shrunk to fit the width, over a faint drop shadow.
void Label(ImDrawList* drawList, ImVec2 center, const char* text, float pixels, float maxWidth) {
    ImFont* font = GetBoldFont() ? GetBoldFont() : ImGui::GetFont();
    ImVec2 extent = font->CalcTextSizeA(pixels, FLT_MAX, 0.0f, text);
    if (extent.x > maxWidth && extent.x > 0.0f) {
        const float k = maxWidth / extent.x;
        pixels *= k;
        extent = extent * k;
    }
    const ImVec2 position = center - extent * 0.5f;
    drawList->AddText(font, pixels, position + ImVec2(0.0f, std::max(1.0f, pixels * 0.05f)),
                      ToU32({0.0f, 0.0f, 0.0f, 0.30f}), text);
    drawList->AddText(font, pixels, position, White, text);
}

// Traces a page's outline: rounded corners, except the top right one, cut at 45 degrees by f for the fold.
void PagePath(ImDrawList* drawList, ImVec2 a, ImVec2 b, float r, float f) {
    drawList->PathClear();
    drawList->PathArcToFast({a.x + r, a.y + r}, r, 6, 9);
    drawList->PathLineTo({b.x - f, a.y});
    drawList->PathLineTo({b.x, a.y + f});
    drawList->PathArcToFast({b.x - r, b.y - r}, r, 0, 3);
    drawList->PathArcToFast({a.x + r, b.y - r}, r, 3, 6);
}

struct Box {
    ImVec2 min;
    ImVec2 max;
};

// A paper page with a folded top right corner, on a soft shadow. Returns the page's box.
Box Page(ImDrawList* drawList, ImVec2 min, float size) {
    const float w = size * 0.60f;
    const float h = size * 0.76f;
    const ImVec2 a = min + ImVec2((size - w) * 0.5f, size * 0.09f);
    const ImVec2 b = a + ImVec2(w, h);
    const float r = size * 0.045f;
    const float f = w * 0.30f;

    // The shadow has the page's shape, so none shows behind the cut corner: outlines grown from inset to outset.
    // Growing the outline by g pushes the 45 degree cut out by g, which lengthens it by g * (2 - sqrt(2)).
    const ImVec2 drop{0.0f, size * 0.03f};
    const float blur = size * 0.09f;
    constexpr int Layers = 6;
    const ImU32 shadow = ToU32({0.0f, 0.0f, 0.0f, 0.16f});
    for (int i = 0; i < Layers; ++i) {
        const float grow = blur * (static_cast<float>(i + 1) / Layers - 0.5f);
        PagePath(drawList, a + drop - ImVec2(grow, grow), b + drop + ImVec2(grow, grow), std::max(0.0f, r + grow),
                 f + grow * 0.586f);
        drawList->PathFillConvex(shadow);
    }

    PagePath(drawList, a, b, r, f);
    FillPath(drawList, a, {a.x, b.y}, PaperTop, PaperBottom);
    HorizontalLine(drawList, a.x + r, b.x - f, a.y + 0.5f, {1.0f, 1.0f, 1.0f, 0.6f});

    // The flap, the paper's back, casts a faint shadow on the page and is lit along its crease.
    const ImVec2 top{b.x - f, a.y};
    const ImVec2 corner{b.x - f, a.y + f};
    const ImVec2 right{b.x, a.y + f};
    const ImVec2 offset{-f * 0.07f, f * 0.11f};
    const ImVec4 flapShadow{0.0f, 0.0f, 0.0f, 0.22f};
    Poly(drawList, {top + offset, right + offset, corner + offset}, top, corner, flapShadow, flapShadow);
    Poly(drawList, {top, right, corner}, corner, (top + right) * 0.5f, Rgb(178, 184, 195), Rgb(236, 239, 244));
    return {a, b};
}

// Short strokes standing in for text; with a keyword color, as code, each line starts with a tinted token.
void TextLines(ImDrawList* drawList, Box page, float size, int lines = 3, const ImVec4* keyword = nullptr) {
    constexpr std::array Lengths{0.44f, 0.66f, 0.38f, 0.60f, 0.50f};
    const float w = page.max.x - page.min.x;
    const float h = page.max.y - page.min.y;
    const float thickness = std::max(1.0f, size * 0.026f);
    const float x = page.min.x + w * 0.14f;
    for (int i = 0; i < lines && i < static_cast<int>(Lengths.size()); ++i) {
        const float y = page.min.y + h * (0.22f + static_cast<float>(i) * 0.09f);
        float start = x;
        if (keyword) {
            const float split = x + w * (i == 1 ? 0.24f : 0.16f);
            drawList->AddRectFilled({start, y}, {split, y + thickness}, ToU32(Darken(*keyword, 0.9f)),
                                    thickness * 0.5f);
            start = split + w * 0.05f;
        }
        drawList->AddRectFilled({start, y}, {x + w * Lengths[static_cast<std::size_t>(i)], y + thickness},
                                ToU32(WithAlpha(Ink, 0.9f)), thickness * 0.5f);
    }
}

// The badge's center and half extents on a page: centered, in the lower part of the sheet.
Box BadgeBox(Box page, float halfHeight) {
    const float w = page.max.x - page.min.x;
    const float h = page.max.y - page.min.y;
    return {{(page.min.x + page.max.x) * 0.5f, page.min.y + h * 0.69f}, {w * 0.40f, h * halfHeight}};
}

// A rounded, lit plate in the accent color, on a soft shadow.
void Badge(ImDrawList* drawList, ImVec2 center, ImVec2 half, ImVec4 accent, float size) {
    const ImVec2 a = center - half;
    const ImVec2 b = center + half;
    const float r = half.y * 0.45f;
    const ImVec2 drop{0.0f, size * 0.012f};
    SoftShadow(drawList, a + drop, b + drop, r, size * 0.035f, 0.45f);
    Rect(drawList, a, b, r, Lighten(accent, 0.12f), Darken(accent, 0.72f));
    HorizontalLine(drawList, a.x + r, b.x - r, a.y + 0.5f, {1.0f, 1.0f, 1.0f, 0.35f});
}

// An isometric cube around the center of its hexagon, with the edge e: lit top, mid left face, shaded right face.
void IsoCube(ImDrawList* drawList, ImVec2 c, float e, ImVec4 color) {
    const float k = e * 0.8660254f;
    const ImVec2 top = c + ImVec2(0.0f, -e);
    const ImVec2 upperRight = c + ImVec2(k, -e * 0.5f);
    const ImVec2 upperLeft = c + ImVec2(-k, -e * 0.5f);
    const ImVec2 lowerRight = c + ImVec2(k, e * 0.5f);
    const ImVec2 lowerLeft = c + ImVec2(-k, e * 0.5f);
    const ImVec2 bottom = c + ImVec2(0.0f, e);

    Poly(drawList, {top, upperRight, c, upperLeft}, upperLeft, upperRight, Lighten(color, 0.30f),
         Lighten(color, 0.08f));
    Poly(drawList, {upperLeft, c, bottom, lowerLeft}, upperLeft, lowerLeft, Darken(color, 0.80f), Darken(color, 0.60f));
    Poly(drawList, {c, upperRight, lowerRight, bottom}, c, bottom, Darken(color, 0.56f), Darken(color, 0.40f));

    const float thickness = std::max(1.0f, e * 0.04f);
    const ImU32 lit = ToU32({1.0f, 1.0f, 1.0f, 0.50f});
    const ImU32 faint = ToU32({1.0f, 1.0f, 1.0f, 0.22f});
    drawList->AddLine(upperLeft, c, lit, thickness);
    drawList->AddLine(c, upperRight, lit, thickness);
    drawList->AddLine(c, bottom, ToU32({1.0f, 1.0f, 1.0f, 0.16f}), thickness);
    drawList->AddLine(upperLeft, top, faint, thickness);
    drawList->AddLine(top, upperRight, faint, thickness);
}

// A sphere lit from the upper left: stacked circles drifting toward the highlight, and a specular spot.
void Sphere(ImDrawList* drawList, ImVec2 c, float radius, ImVec4 color) {
    const ImVec4 shadow = Darken(color, 0.28f);
    const ImVec4 lit = Lighten(color, 0.30f);
    const ImVec2 highlight = c + ImVec2(-0.34f, -0.38f) * radius;
    constexpr int Layers = 24;
    const int segments = std::clamp(static_cast<int>(radius * 1.2f), 24, 64);
    for (int i = 0; i < Layers; ++i) {
        const float t = static_cast<float>(i) / (Layers - 1);
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        drawList->AddCircleFilled(Lerp(c, highlight, t), radius * (1.0f - 0.88f * t), ToU32(Lerp(shadow, lit, eased)),
                                  segments);
    }
    drawList->AddCircleFilled(highlight, radius * 0.16f, ToU32({1.0f, 1.0f, 1.0f, 0.18f}), segments);
    drawList->AddCircleFilled(highlight, radius * 0.08f, ToU32({1.0f, 1.0f, 1.0f, 0.55f}), segments);
}

// A cube on a contact shadow, for models and meshes.
void Block(ImDrawList* drawList, ImVec2 min, float size, ImVec4 color) {
    const float e = size * 0.29f;
    const ImVec2 c = min + ImVec2(size * 0.5f, size * 0.47f);
    EllipseShadow(drawList, c + ImVec2(0.0f, e * 0.80f), {e * 1.10f, e * 0.36f}, 0.7f);
    IsoCube(drawList, c, e, color);
}

// A material: a lit sphere tinted with the material color.
void DrawMaterial(ImDrawList* drawList, ImVec2 min, float size) {
    const float radius = size * 0.31f;
    const ImVec2 c = min + ImVec2(size * 0.5f, size * 0.46f);
    EllipseShadow(drawList, c + ImVec2(0.0f, radius * 0.96f), {radius * 0.85f, radius * 0.22f}, 0.65f);
    Sphere(drawList, c, radius, Lerp(Clay, MaterialAccent, 0.35f));
}

// A picture: a matted photo of hills under a sun.
void DrawTexture(ImDrawList* drawList, ImVec2 min, float size) {
    const ImVec2 a = min + ImVec2(size * 0.12f, size * 0.18f);
    const ImVec2 b = min + ImVec2(size * 0.88f, size * 0.82f);
    const float r = size * 0.045f;
    const ImVec2 drop{0.0f, size * 0.03f};
    SoftShadow(drawList, a + drop, b + drop, r, size * 0.09f, 0.6f);
    Rect(drawList, a, b, r, PaperTop, PaperBottom);
    HorizontalLine(drawList, a.x + r, b.x - r, a.y + 0.5f, {1.0f, 1.0f, 1.0f, 0.6f});

    const float m = size * 0.055f;
    const ImVec2 ia = a + ImVec2(m, m);
    const ImVec2 ib = b - ImVec2(m, m);
    const float iw = ib.x - ia.x;
    const float ih = ib.y - ia.y;
    Rect(drawList, ia, ib, 0.0f, Rgb(62, 104, 150), Rgb(150, 184, 204));

    drawList->PushClipRect(ia, ib, true);
    const ImVec2 sun{ia.x + iw * 0.74f, ia.y + ih * 0.30f};
    drawList->AddCircleFilled(sun, size * 0.085f, ToU32({1.0f, 0.9f, 0.7f, 0.18f}), 32);
    drawList->AddCircleFilled(sun, size * 0.05f, ToU32(Rgb(255, 228, 170)), 32);
    Poly(drawList, {{ia.x + iw * 0.20f, ib.y}, {ia.x + iw * 0.58f, ia.y + ih * 0.32f}, {ia.x + iw * 1.05f, ib.y}},
         {0.0f, ia.y + ih * 0.32f}, {0.0f, ib.y}, Rgb(96, 122, 150), Rgb(58, 76, 100));
    Poly(drawList, {{ia.x - iw * 0.15f, ib.y}, {ia.x + iw * 0.28f, ia.y + ih * 0.50f}, {ia.x + iw * 0.78f, ib.y}},
         {0.0f, ia.y + ih * 0.50f}, {0.0f, ib.y}, Lighten(TextureAccent, 0.05f), Darken(TextureAccent, 0.55f));
    drawList->PopClipRect();
    drawList->AddRect(ia, ib, ToU32({0.0f, 0.0f, 0.0f, 0.28f}), 0.0f);
}

// A scene: a little view onto a ground grid under an evening sky, with a cube and a sphere.
void DrawScene(ImDrawList* drawList, ImVec2 min, float size) {
    const ImVec2 a = min + ImVec2(size * 0.10f, size * 0.17f);
    const ImVec2 b = min + ImVec2(size * 0.90f, size * 0.83f);
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    const float r = size * 0.06f;
    const ImVec2 drop{0.0f, size * 0.03f};
    SoftShadow(drawList, a + drop, b + drop, r, size * 0.09f, 0.65f);

    const float horizon = a.y + h * 0.47f;
    Rect(drawList, a, {b.x, horizon}, r, Rgb(30, 44, 74), Rgb(104, 132, 172), ImDrawFlags_RoundCornersTop);
    Rect(drawList, {a.x, horizon}, b, r, Rgb(58, 64, 76), Rgb(28, 31, 38), ImDrawFlags_RoundCornersBottom);

    // A low sun with a soft glow.
    const ImVec2 sun{a.x + w * 0.77f, a.y + h * 0.22f};
    const float sunRadius = size * 0.045f;
    for (int i = 4; i >= 1; --i) {
        drawList->AddCircleFilled(sun, sunRadius * (1.0f + static_cast<float>(i) * 0.45f),
                                  ToU32({1.0f, 0.86f, 0.66f, 0.07f}), 32);
    }
    drawList->AddCircleFilled(sun, sunRadius, ToU32(Rgb(255, 236, 206)), 32);

    // A perspective grid converging on the horizon, fading into the distance.
    const float thickness = std::max(1.0f, size * 0.011f);
    drawList->PushClipRect({a.x, horizon}, b, true);
    const int start = drawList->VtxBuffer.Size;
    const ImVec2 vanish{(a.x + b.x) * 0.5f, horizon};
    for (int k = -6; k <= 6; ++k) {
        drawList->AddLine(vanish, {vanish.x + static_cast<float>(k) * w * 0.19f, b.y + h * 0.1f}, White, thickness);
    }
    for (const float depth : {1.3f, 1.9f, 3.0f, 5.0f}) {
        const float y = horizon + (b.y - horizon) / depth;
        drawList->AddLine({a.x, y}, {b.x, y}, White, thickness);
    }
    const ImVec4 grid = Lighten(SceneAccent, 0.15f);
    ApplyGradient(drawList, start, vanish, {vanish.x, b.y}, WithAlpha(grid, 0.0f), WithAlpha(grid, 0.55f));
    drawList->PopClipRect();
    HorizontalLine(drawList, a.x, b.x, horizon, {1.0f, 0.9f, 0.8f, 0.25f});

    // A cube and a sphere standing on the grid, each on a contact shadow.
    const float e = size * 0.105f;
    const ImVec2 cube{a.x + w * 0.36f, horizon + (b.y - horizon) * 0.62f - e};
    EllipseShadow(drawList, cube + ImVec2(0.0f, e * 0.85f), {e * 1.15f, e * 0.38f}, 0.6f);
    IsoCube(drawList, cube, e, Clay);

    const float ballRadius = size * 0.075f;
    const ImVec2 ball{a.x + w * 0.66f, horizon + (b.y - horizon) * 0.64f - ballRadius};
    EllipseShadow(drawList, ball + ImVec2(0.0f, ballRadius * 0.92f), {ballRadius, ballRadius * 0.30f}, 0.6f);
    Sphere(drawList, ball, ballRadius, Rgb(222, 146, 92));

    drawList->AddRect(a, b, ToU32({1.0f, 1.0f, 1.0f, 0.10f}), r);
}

// A sound: a page with a waveform badge.
void DrawSound(ImDrawList* drawList, ImVec2 min, float size) {
    const Box page = Page(drawList, min, size);
    TextLines(drawList, page, size);
    const auto [center, half] = BadgeBox(page, 0.15f);
    Badge(drawList, center, half, SoundAccent, size);

    constexpr std::array Heights{0.30f, 0.55f, 0.90f, 0.50f, 1.00f, 0.70f, 0.40f, 0.80f, 0.35f};
    const float barWidth = std::max(1.0f, half.x * 0.085f);
    const float step = half.x * 1.60f / static_cast<float>(Heights.size() - 1);
    float x = center.x - half.x * 0.80f;
    for (const float k : Heights) {
        const float barHeight = std::max(barWidth, half.y * 1.30f * k);
        drawList->AddRectFilled({x - barWidth * 0.5f, center.y - barHeight * 0.5f},
                                {x + barWidth * 0.5f, center.y + barHeight * 0.5f}, ToU32({1.0f, 1.0f, 1.0f, 0.95f}),
                                barWidth * 0.5f);
        x += step;
    }
}

// A font: a page with an "Aa" badge.
void DrawFont(ImDrawList* drawList, ImVec2 min, float size) {
    const Box page = Page(drawList, min, size);
    TextLines(drawList, page, size);
    const auto [center, half] = BadgeBox(page, 0.15f);
    Badge(drawList, center, half, FontAccent, size);
    Label(drawList, center, "Aa", half.y * 1.85f, half.x * 1.55f);
}

void DrawDocument(ImDrawList* drawList, ImVec2 min, float size, const char* label, ImVec4 accent, bool code) {
    const Box page = Page(drawList, min, size);
    TextLines(drawList, page, size, 3, code ? &accent : nullptr);
    const auto [center, half] = BadgeBox(page, 0.14f);
    Badge(drawList, center, half, accent, size);
    Label(drawList, center, label, half.y * 1.85f, half.x * 1.55f);
}

} // namespace

ImVec4 GetFileColor(FileKind kind) {
    switch (kind) {
    case FileKind::Source:
        return SourceAccent;
    case FileKind::Header:
        return HeaderAccent;
    case FileKind::BuildScript:
        return BuildScriptAccent;
    case FileKind::Other:
        break;
    }
    return FileAccent;
}

void DrawFile(ImDrawList* drawList, ImVec2 min, float size, std::string_view name) {
    const FileKind kind = GetFileKind(name);
    const ImVec4 accent = GetFileColor(kind);
    std::string badge;
    switch (kind) {
    case FileKind::Source:
        badge = "C++";
        break;
    case FileKind::Header:
        badge = "H";
        break;
    case FileKind::BuildScript:
        badge = "CMake";
        break;
    case FileKind::Other: {
        // A short extension, of letters and digits, such as "JSON".
        const std::size_t dot = name.find_last_of('.');
        if (dot != std::string_view::npos && dot > 0 && name.size() - dot >= 2 && name.size() - dot <= 5) {
            for (const char c : name.substr(dot + 1)) {
                if (!std::isalnum(static_cast<unsigned char>(c))) {
                    badge.clear();
                    break;
                }
                badge += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        break;
    }
    }
    if (badge.empty()) {
        TextLines(drawList, Page(drawList, min, size), size, 5);
        return;
    }
    const bool code = kind == FileKind::Source || kind == FileKind::Header;
    DrawDocument(drawList, min, size, badge.c_str(), accent, code);
}

ImVec4 GetTypeColor(AssetType type, std::string_view path) {
    if (type == AssetType::Script) {
        if (const EditorScriptLanguage* language = FindEditorScriptLanguage(path))
            return language->color;
    }
    switch (type) {
    case AssetType::Script:
        return SourceAccent;
    case AssetType::SpriteAtlas:
    case AssetType::Texture:
        return TextureAccent;
    case AssetType::Mesh:
        return MeshAccent;
    case AssetType::Material:
        return MaterialAccent;
    case AssetType::Model:
        return ModelAccent;
    case AssetType::Sound:
        return SoundAccent;
    case AssetType::Font:
        return FontAccent;
    case AssetType::Scene:
        return SceneAccent;
    case AssetType::AnimatorController:
    case AssetType::Skeleton:
    case AssetType::AnimationClip:
    case AssetType::Prefab:
        return ModelAccent;
    }
    return Ink;
}

void DrawFolder(ImDrawList* drawList, ImVec2 min, float size, bool hasContents) {
    const float x0 = min.x + size * 0.12f;
    const float x1 = min.x + size * 0.88f;
    const float backTop = min.y + size * 0.27f;
    const float frontTop = backTop + size * 0.11f;
    const float bottom = min.y + size * 0.80f;
    const float w = x1 - x0;
    const float r = size * 0.05f;

    SoftShadow(drawList, {x0, backTop + size * 0.05f}, {x1, bottom + size * 0.035f}, r, size * 0.10f, 0.65f);

    // The back panel and its tab share one gradient, so the seam between the two shapes does not show.
    const ImVec2 g0{x0, backTop - size * 0.08f};
    const ImVec2 g1{x0, bottom};
    const ImVec4 backTopColor = Rgb(138, 112, 74);
    const ImVec4 backBottomColor = Rgb(96, 76, 48);
    const float tabWidth = w * 0.36f;
    const float tabHeight = size * 0.075f;
    drawList->PathClear();
    drawList->PathArcToFast({x0 + r, backTop - tabHeight + r}, r, 6, 9);
    drawList->PathLineTo({x0 + tabWidth, backTop - tabHeight});
    drawList->PathLineTo({x0 + tabWidth + tabHeight * 1.1f, backTop + r});
    drawList->PathLineTo({x0, backTop + r});
    FillPath(drawList, g0, g1, backTopColor, backBottomColor);
    Rect(drawList, {x0, backTop}, {x1, bottom}, r, backTopColor, backBottomColor);
    const ImVec4 edge{1.0f, 1.0f, 1.0f, 0.18f};
    HorizontalLine(drawList, x0 + r, x0 + tabWidth, backTop - tabHeight + 0.5f, edge);
    HorizontalLine(drawList, x0 + tabWidth + tabHeight * 1.1f, x1 - r, backTop + 0.5f, edge);

    if (hasContents) {
        Rect(drawList, {x0 + w * 0.10f, backTop - size * 0.035f}, {x1 - w * 0.16f, frontTop + r}, size * 0.02f,
             Rgb(190, 194, 202), Rgb(160, 165, 175));
        Rect(drawList, {x0 + w * 0.17f, backTop + size * 0.005f}, {x1 - w * 0.08f, frontTop + r}, size * 0.02f,
             Rgb(236, 238, 242), Rgb(204, 208, 215));
    }

    // The front panel: lit along its top edge, falling off toward the bottom.
    Rect(drawList, {x0, frontTop}, {x1, bottom}, r, Rgb(214, 180, 122), Rgb(166, 132, 80));
    HorizontalLine(drawList, x0 + r, x1 - r, frontTop + 0.5f, {1.0f, 1.0f, 1.0f, 0.45f});
    HorizontalLine(drawList, x0 + r, x1 - r, bottom - 0.5f, {0.0f, 0.0f, 0.0f, 0.18f});
}

void DrawAsset(ImDrawList* drawList, ImVec2 min, float size, AssetType type, std::string_view path) {
    switch (type) {
    case AssetType::Script: {
        const EditorScriptLanguage* language = FindEditorScriptLanguage(path);
        const std::string badge = language ? std::string(language->badge) : std::string("CODE");
        DrawDocument(drawList, min, size, badge.c_str(), GetTypeColor(type, path), true);
        return;
    }
    case AssetType::SpriteAtlas:
    case AssetType::Texture:
        DrawTexture(drawList, min, size);
        return;
    case AssetType::Mesh:
        Block(drawList, min, size, MeshAccent);
        return;
    case AssetType::Material:
        DrawMaterial(drawList, min, size);
        return;
    case AssetType::Model:
        Block(drawList, min, size, Clay);
        return;
    case AssetType::Sound:
        DrawSound(drawList, min, size);
        return;
    case AssetType::Font:
        DrawFont(drawList, min, size);
        return;
    case AssetType::Scene:
        DrawScene(drawList, min, size);
        return;
    case AssetType::AnimatorController:
    case AssetType::Skeleton:
    case AssetType::AnimationClip:
    case AssetType::Prefab:
        Block(drawList, min, size, ModelAccent);
        return;
    }
    TextLines(drawList, Page(drawList, min, size), size, 5);
}

} // namespace Spot::Editor::AssetIcons
