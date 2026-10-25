#include "textures.h"

#include "level_textures.h"
#include "texture_painting.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace Generator::Textures {

namespace {

// White, with the coverage the function gives each pixel, from 0 to 1, as alpha.
template <typename F>
Spot::Image PaintCoverage(std::uint32_t size, F&& coverage) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * size + x) * 4];
            pixel[0] = pixel[1] = pixel[2] = 255;
            const float alpha =
                std::clamp(coverage(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f), 0.0f, 1.0f);
            pixel[3] = static_cast<std::uint8_t>(alpha * 255.0f + 0.5f);
        }
    }
    return {size, size, std::move(pixels)};
}

// How much of a pixel lies inside a shape, from its signed distance to the edge in pixels, negative inside.
float Coverage(float distance) {
    return std::clamp(0.5f - distance, 0.0f, 1.0f);
}

} // namespace

// The signed distance to the edge of the rectangle inset from a square image, its corners rounded by the radius.
float RoundedInset(float x, float y, float size, float inset, float radius) {
    const float half = size * 0.5f;
    const float dx = std::abs(x - half) - (half - inset - radius);
    const float dy = std::abs(y - half) - (half - inset - radius);
    return std::sqrt(std::max(dx, 0.0f) * std::max(dx, 0.0f) + std::max(dy, 0.0f) * std::max(dy, 0.0f)) +
           std::min(std::max(dx, dy), 0.0f) - radius;
}

Spot::Image UiPanel() {
    constexpr std::uint32_t PanelSize = 64;
    // Straight edges on whole pixels stay crisp; the corners are smooth.
    return PaintCoverage(PanelSize, [&](float x, float y) {
        return Coverage(RoundedInset(x, y, static_cast<float>(PanelSize), 0.0f, 4.0f));
    });
}

Spot::Image UiOutline() {
    constexpr std::uint32_t PanelSize = 64;
    // The panel's outermost pixels.
    return PaintCoverage(PanelSize, [&](float x, float y) {
        return Coverage(std::abs(RoundedInset(x, y, static_cast<float>(PanelSize), 0.0f, 4.0f) + 0.5f) - 0.5f);
    });
}

Spot::Image UiShadow() {
    constexpr std::uint32_t ShadowSize = 64;
    return PaintCoverage(ShadowSize, [&](float x, float y) {
        const float outside = std::max(RoundedInset(x, y, static_cast<float>(ShadowSize), 16.0f, 4.0f), 0.0f);
        const float t = 1.0f - std::min(outside / 16.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    });
}

Spot::Image UiDot() {
    constexpr std::uint32_t DotSize = 64;
    const float half = static_cast<float>(DotSize) * 0.5f;
    return PaintCoverage(DotSize, [&](float x, float y) {
        return Coverage(std::sqrt((x - half) * (x - half) + (y - half) * (y - half)) - (half - 1.0f));
    });
}

Spot::Image UiHitMarker() {
    constexpr std::uint32_t MarkerSize = 64;
    const float half = static_cast<float>(MarkerSize) * 0.5f;
    return PaintCoverage(MarkerSize, [&](float x, float y) {
        // Four strokes along the diagonals, leaving the middle open.
        const float u = std::abs(x - half);
        const float v = std::abs(y - half);
        const float along = (u + v) / std::sqrt(2.0f);
        const float across = std::abs(u - v) / std::sqrt(2.0f);
        return Coverage(std::max({across - 2.5f, half * 0.4f - along, along - (half - 3.0f)}));
    });
}

Spot::Image UiScrim() {
    constexpr std::uint32_t ScrimSize = 128;
    const float half = static_cast<float>(ScrimSize) * 0.5f;
    return PaintCoverage(ScrimSize, [&](float x, float y) {
        const float t = 1.0f - std::min(std::sqrt((x - half) * (x - half) + (y - half) * (y - half)) / half, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    });
}

Spot::Image ChainLink() {
    constexpr std::uint32_t LinkSize = 64;
    const float size = static_cast<float>(LinkSize);
    return PaintCoverage(LinkSize, [&](float x, float y) {
        // One diamond per tile: wires along the diagonals, wrapping, so the mesh continues across tiles.
        const float u = x / size;
        const float v = y / size;
        const float down = std::abs(u - v);
        const float up = std::abs(u + v - 1.0f);
        const float wire = std::min({down, 1.0f - down, up, 1.0f - up}) * size / std::sqrt(2.0f);
        return Coverage(wire - 1.6f);
    });
}

namespace {

// Deck boards along u, eight across, each in two lengths butted end to end, nailed down at their ends, weathering
// toward silver, with gaps between them.
SurfacePoint WoodPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    constexpr int Boards = 8;
    const float across = v * Boards;
    const int board = static_cast<int>(std::floor(across));
    const float inBoard = across - static_cast<float>(board);
    const float joint = Random(board, 0, 901);
    const float along = u - joint - std::floor(u - joint);
    const int piece = board * 2 + (along < 0.5f ? 0 : 1);
    const auto seed = static_cast<std::uint32_t>(910 + piece * 16);
    const float fromEnd = std::min(along < 0.5f ? along : along - 0.5f, along < 0.5f ? 0.5f - along : 1.0f - along);
    const float fromSide = std::min(inBoard, 1.0f - inBoard);

    const WoodFigure figure = WoodGrain(u + Random(piece, 1, 902), v, 90, seed);
    const float silver = Smooth(0.45f, 0.75f, Noise(u, v, 3, 6, 3, seed + 5)) * 0.45f;
    Rgb color = Mix(Rgb{0.63f, 0.47f, 0.31f}, Rgb{0.36f, 0.23f, 0.13f}, figure.tone);
    color = color * ((0.84f + Random(piece, 2, 903) * 0.26f) * (1.0f - figure.pores * 0.22f));
    color = Mix(color, Rgb{0.52f, 0.5f, 0.47f} * (0.85f + figure.fibers * 0.25f), silver);
    // Each board is rounded off along its sides and ends, and cupped a little across.
    const float rounded = Smooth(0.0f, 0.1f, fromSide) * Smooth(0.0f, 0.012f, fromEnd);
    SurfacePoint point{.color = color,
                       .height = rounded * 2.0f - (fromSide - 0.5f) * (fromSide - 0.5f) * 1.2f + figure.tone * 0.35f -
                                 figure.pores * 0.4f,
                       .roughness = 0.68f + figure.pores * 0.12f + silver * 0.15f};
    // Two nails at each end of each length, in a rust stain.
    for (const float end : {0.025f, 0.475f, 0.525f, 0.975f}) {
        for (const float side : {0.27f, 0.73f}) {
            const float du = (along - end) * 6.0f;
            const float dv = (inBoard - side) * 0.75f;
            const float distance = std::sqrt(du * du + dv * dv);
            if (distance < 0.05f) {
                const float stain = Smooth(0.05f, 0.02f, distance);
                point.color = Mix(point.color, Rgb{0.3f, 0.17f, 0.09f}, stain * 0.6f);
                if (distance < 0.022f) {
                    point.color = Rgb{0.2f, 0.19f, 0.18f} * (0.8f + Random(x, y, 904) * 0.3f);
                    point.height -= 0.4f;
                    point.roughness = 0.55f;
                    point.metallic = 0.6f;
                }
            }
        }
    }
    if (fromSide < 0.035f || fromEnd < 0.0025f) {
        point.color = Rgb{0.09f, 0.07f, 0.05f};
        point.height = -4.0f;
        point.occlusion = 0.3f;
        point.roughness = 0.95f;
        point.metallic = 0.0f;
    }
    return point;
}

// Walnut, oiled smooth: fine grain without joints, as gun stocks and handles are carved from.
SurfacePoint WalnutPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const WoodFigure figure = WoodGrain(u, v, 40, 1950);
    // Curl across the grain catches the light in bands.
    const float curl = Noise(u, v, 24, 3, 2, 1951);
    Rgb color = Mix(Rgb{0.5f, 0.3f, 0.17f}, Rgb{0.2f, 0.1f, 0.05f}, figure.tone);
    color = color * ((0.9f + curl * 0.15f) * (1.0f - figure.pores * 0.3f));
    return {.color = color,
            .height = figure.tone * 0.25f - figure.pores * 0.35f + curl * 0.2f,
            .roughness = 0.32f + figure.pores * 0.25f + Random(x, y, 1952) * 0.04f};
}

// Paint over steel: the ripples of a sprayed coat, fine scratches, and chips worn through to the bare metal.
SurfacePoint PaintedMetalPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float peel = GradientNoise(u * 160.0f, v * 160.0f, 160, 160, 910);
    const float shade = Noise(u, v, 4, 4, 4, 911);
    const float wear = Noise(u, v, 6, 6, 5, 912);
    // Scratches run every way: along both diagonals as well as straight.
    const float scratched = Smooth(0.55f, 0.7f, Noise(u, v, 5, 5, 2, 913));
    const float scratches = std::max({Smooth(0.975f, 0.995f, Ridges(u + v, u - v, 3, 48, 1, 914)),
                                      Smooth(0.975f, 0.995f, Ridges(u - v, u + v, 3, 40, 1, 915)),
                                      Smooth(0.98f, 0.997f, Ridges(u, v, 4, 56, 1, 916))}) *
                            scratched;
    SurfacePoint point{.color = Rgb{0.8f, 0.8f, 0.78f} * (0.93f + shade * 0.1f + peel * 0.03f),
                       .height = peel * 0.35f + shade * 0.4f,
                       .roughness = 0.4f + shade * 0.1f + peel * 0.04f};
    if (wear > 0.8f || scratches > 0.3f) {
        // The chip's edge is a step down to the steel, which is dark at the edge of the paint.
        const float bare = std::max(Smooth(0.8f, 0.84f, wear), scratches);
        point.color = Mix(point.color * 0.7f, Rgb{0.6f, 0.6f, 0.62f} * (0.9f + peel * 0.2f), bare);
        point.height -= bare * 0.8f;
        point.roughness = 0.4f - bare * 0.12f;
        point.metallic = bare;
    }
    return point;
}

// Steel eaten by rust: blue-grey mill scale, then rust in layers, flaking and pitted, running down in streaks.
SurfacePoint RustyMetalPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float body = Noise(u, v, 4, 4, 5, 920);
    const float streaks = Noise(u, v, 24, 3, 3, 921);
    const float pits = Smooth(0.6f, 0.8f, GradientNoise(u * 120.0f, v * 120.0f, 120, 120, 922));
    const float millScale = Noise(u, v, 10, 10, 3, 923);
    const float rust = Smooth(0.42f, 0.58f, body * 0.75f + streaks * 0.35f);
    const float flake = Smooth(0.6f, 0.7f, Noise(u, v, 12, 12, 3, 924)) * rust;
    const Rgb mill = Mix(Rgb{0.27f, 0.29f, 0.31f}, Rgb{0.45f, 0.46f, 0.47f}, millScale);
    Rgb rusted = Mix(Rgb{0.56f, 0.29f, 0.12f}, Rgb{0.3f, 0.14f, 0.07f}, pits * 0.7f + streaks * 0.3f);
    rusted = Mix(rusted, Rgb{0.68f, 0.42f, 0.2f}, flake * 0.6f);
    return {.color = Mix(mill, rusted, rust) * (0.9f + Random(x, y, 925) * 0.1f),
            .height = rust * (1.0f + flake * 1.5f) - pits * rust * 1.2f + millScale * 0.3f,
            .occlusion = 1.0f - pits * rust * 0.25f,
            .roughness = 0.45f + millScale * 0.1f + rust * 0.45f,
            .metallic = 1.0f - rust};
}

// Cast concrete, as of barriers, ramps and steps: mottled, with sand and the odd stone showing, air bubbles left by
// the form, hairline cracks, and streaks where rain ran down it.
SurfacePoint ConcretePoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float mottle = Noise(u, v, 3, 3, 5, 930);
    const float fine = Noise(u, v, 24, 24, 3, 931);
    const float sand = Random(x, y, 932);
    const float streaks =
        Smooth(0.55f, 0.8f, Noise(u, v, 18, 2, 3, 933)) * Smooth(0.4f, 0.7f, Noise(u, v, 2, 2, 2, 934));
    const CellSample stones = Cells(u * 70.0f, v * 70.0f, 70, 935);
    const float stone = Smooth(0.22f, 0.14f, stones.nearest) * Smooth(0.72f, 0.78f, stones.id);
    const CellSample bubbles = Cells(u * 36.0f, v * 36.0f, 36, 936);
    const float bubbleRadius = 0.06f + Random(static_cast<int>(bubbles.id * 65536.0f), 0, 937) * 0.1f;
    const float bubble = Smooth(bubbleRadius, bubbleRadius * 0.5f, bubbles.nearest) * Smooth(0.8f, 0.84f, bubbles.id);
    const float warpU = (Noise(u, v, 5, 5, 3, 938) - 0.5f) * 0.4f;
    const float warpV = (Noise(u, v, 5, 5, 3, 939) - 0.5f) * 0.4f;
    const CellSample plates = Cells(u * 3.0f + warpU, v * 3.0f + warpV, 3, 940);
    const float crack =
        (1.0f - Smooth(0.0f, 0.012f, plates.second - plates.nearest)) * Smooth(0.55f, 0.7f, Noise(u, v, 4, 4, 3, 941));
    Rgb color = Rgb{0.62f, 0.61f, 0.58f} * (0.8f + mottle * 0.28f + fine * 0.08f + sand * 0.06f);
    color = Mix(color, Rgb{0.7f, 0.67f, 0.6f} * (0.8f + stones.id * 0.3f), stone * 0.5f);
    color = color * (1.0f - streaks * 0.14f);
    color = Mix(color, Rgb{0.22f, 0.21f, 0.2f}, std::max(bubble * 0.75f, crack * 0.7f));
    return {.color = color,
            .height = mottle * 1.5f + fine * 0.8f + sand * 0.4f + stone * 0.6f - bubble * 2.5f - crack * 1.2f,
            .occlusion = 1.0f - bubble * 0.5f - crack * 0.3f,
            .roughness = 0.84f + sand * 0.08f + streaks * 0.04f - stone * 0.1f};
}

// Dark rubber: a fine grain from the mold, with smoother patches where it rubs.
SurfacePoint RubberPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float grain = GradientNoise(u * 200.0f, v * 200.0f, 200, 200, 940);
    const float rubbed = Smooth(0.5f, 0.75f, Noise(u, v, 6, 6, 3, 941));
    const float scuffs = Smooth(0.97f, 0.99f, Ridges(u + v, u - v, 4, 24, 1, 942)) * rubbed;
    return {.color = Rgb{0.1f, 0.1f, 0.105f} * (0.9f + grain * 0.15f + scuffs * 0.6f),
            .height = grain * 0.5f * (1.0f - rubbed * 0.6f),
            .roughness = 0.82f - rubbed * 0.18f + grain * 0.05f};
}

// Clear glass, with faint smudges that scatter a little light and gentle waves in the pane.
SurfacePoint GlassPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float smudge = Fractal(u, v, 4, 4, 4, 950);
    const float wave = Fractal(u, v, 3, 3, 2, 951);
    return {.color = Rgb{0.95f, 0.97f, 0.98f}, .height = wave * 1.5f, .roughness = 0.04f + smudge * smudge * 0.25f};
}

// The stippled polymer of grips and frames: small rounded bumps packed close, for a hold that does not slip.
SurfacePoint PolymerPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const CellSample stipple = Cells(u * 110.0f, v * 110.0f, 110, 960);
    const float bump = Smooth(0.0f, 0.4f, stipple.second - stipple.nearest);
    const float rubbed = Smooth(0.55f, 0.8f, Noise(u, v, 5, 5, 3, 961));
    return {.color = Rgb{0.11f, 0.11f, 0.115f} * (0.88f + bump * 0.18f + stipple.id * 0.05f),
            .height = std::sqrt(bump) * 1.4f * (1.0f - rubbed * 0.4f),
            .occlusion = 0.8f + bump * 0.2f,
            .roughness = 0.72f - bump * 0.12f - rubbed * 0.12f};
}

// A gun's metal finish: bead-blasted to a fine even speckle, with faint tool marks along it and polished patches
// where hands and holsters rub.
SurfacePoint GunmetalPoint(int x, int y, float scale) {
    const float u = Coordinate(x, scale);
    const float v = Coordinate(y, scale);
    const float speckle = Random(x, y, 970);
    const float blast = GradientNoise(u * 240.0f, v * 240.0f, 240, 240, 971);
    const float tooling = Ridges(u, v, 2, 90, 2, 972);
    const float polished = Smooth(0.62f, 0.85f, Noise(u, v, 4, 4, 4, 973));
    return {.color = Rgb{0.56f, 0.56f, 0.57f} * (0.92f + blast * 0.08f + speckle * 0.04f + polished * 0.12f),
            .height = blast * 0.3f + tooling * 0.12f,
            .roughness = 0.5f + blast * 0.08f + speckle * 0.05f - polished * 0.2f - tooling * 0.04f,
            .metallic = 0.85f + polished * 0.15f};
}

} // namespace

std::string_view GetSurfaceName(Surface surface) {
    switch (surface) {
    case Surface::Wood:
        return "wood";
    case Surface::PaintedMetal:
        return "painted_metal";
    case Surface::RustyMetal:
        return "rusty_metal";
    case Surface::Concrete:
        return "concrete";
    case Surface::Rubber:
        return "rubber";
    case Surface::Glass:
        return "glass";
    case Surface::Grass:
        return "grass";
    case Surface::Gravel:
        return "gravel";
    case Surface::Asphalt:
        return "asphalt";
    case Surface::Paving:
        return "paving";
    case Surface::Brick:
        return "brick";
    case Surface::Corrugated:
        return "corrugated";
    case Surface::TreadPlate:
        return "tread_plate";
    case Surface::Rock:
        return "rock";
    case Surface::Bark:
        return "bark";
    case Surface::Leaves:
        return "leaves";
    case Surface::Water:
        return "water";
    case Surface::Slab:
        return "slab";
    case Surface::Needles:
        return "needles";
    case Surface::Polymer:
        return "polymer";
    case Surface::Gunmetal:
        return "gunmetal";
    case Surface::Walnut:
        return "walnut";
    }
    return "";
}

std::uint32_t GetSurfaceSize(Surface surface) {
    switch (surface) {
    case Surface::Grass:
    case Surface::Slab:
    case Surface::Bark:
        return 2048;
    case Surface::Wood:
    case Surface::PaintedMetal:
    case Surface::RustyMetal:
    case Surface::Concrete:
    case Surface::Rubber:
    case Surface::Polymer:
    case Surface::Gunmetal:
    case Surface::Walnut:
    case Surface::Rock:
    case Surface::Asphalt:
    case Surface::Leaves:
    case Surface::Needles:
        return 1024;
    case Surface::Glass:
        return 256;
    default:
        return 512;
    }
}

SurfaceTextures PaintSurface(Surface surface, std::uint32_t size) {
    switch (surface) {
    case Surface::Wood:
        return PaintSurfaceTexels(size, WoodPoint, {{.radius = 3.0f, .depth = 2.0f, .dirt = 0.35f, .occlusion = 0.4f}});
    case Surface::Walnut:
        return PaintSurfaceTexels(size, WalnutPoint);
    case Surface::PaintedMetal:
        return PaintSurfaceTexels(size, PaintedMetalPoint, {{.radius = 2.0f, .depth = 0.6f, .dirt = 0.3f}});
    case Surface::RustyMetal:
        return PaintSurfaceTexels(size, RustyMetalPoint,
                                  {{.radius = 3.0f, .depth = 1.0f, .dirt = 0.3f, .occlusion = 0.3f, .wear = 0.1f}});
    case Surface::Concrete:
        return PaintSurfaceTexels(size, ConcretePoint,
                                  {{.radius = 2.0f, .depth = 1.5f, .dirt = 0.35f, .occlusion = 0.4f}});
    case Surface::Rubber:
        return PaintSurfaceTexels(size, RubberPoint);
    case Surface::Glass:
        return PaintSurfaceTexels(size, GlassPoint);
    case Surface::Polymer:
        return PaintSurfaceTexels(size, PolymerPoint,
                                  {{.radius = 1.5f, .depth = 0.8f, .dirt = 0.25f, .occlusion = 0.3f}});
    case Surface::Gunmetal:
        return PaintSurfaceTexels(size, GunmetalPoint);
    default:
        break;
    }
    return PaintLevelSurface(surface, size);
}

} // namespace Generator::Textures
