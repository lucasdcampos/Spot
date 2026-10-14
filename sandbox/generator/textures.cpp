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

template <typename F>
Spot::Image Paint(F&& color) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(Size) * Size * 4);
    for (std::uint32_t y = 0; y < Size; ++y) {
        for (std::uint32_t x = 0; x < Size; ++x) {
            const Rgb rgb = color(static_cast<int>(x), static_cast<int>(y));
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * Size + x) * 4];
            const auto byte = [](float value) {
                return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            pixel[0] = byte(rgb.r);
            pixel[1] = byte(rgb.g);
            pixel[2] = byte(rgb.b);
            pixel[3] = 255;
        }
    }
    return {Size, Size, std::move(pixels)};
}

// The texture's coordinates, from 0 to 1, at the pixel's center.
float U(int x) {
    return (static_cast<float>(x) + 0.5f) / static_cast<float>(Size);
}

float Distance(int x, int y, float cx, float cy) {
    const float dx = static_cast<float>(x) + 0.5f - cx;
    const float dy = static_cast<float>(y) + 0.5f - cy;
    return std::sqrt(dx * dx + dy * dy);
}

// Pixels of the round props' ends, as Generator::Shapes lays them out.
constexpr float CapRadius = 0.24f * static_cast<float>(Size);
constexpr float TopCapX = 0.25f * static_cast<float>(Size);
constexpr float BottomCapX = 0.75f * static_cast<float>(Size);
constexpr float CapY = 0.75f * static_cast<float>(Size);
constexpr int SideHeight = static_cast<int>(Size) / 2;

// How far the pixel is from the center of the nearest end, in pixels.
float CapDistance(int x, int y) {
    return std::min(Distance(x, y, TopCapX, CapY), Distance(x, y, BottomCapX, CapY));
}

Rgb Wood(int x, int y, Rgb dark, Rgb light, std::uint32_t seed) {
    // Grain runs along X: the noise changes slowly across it and quickly down it.
    const float grain = Fractal(U(x), U(y), 2, 24, 3, seed);
    const float rings = grain * 6.0f - std::floor(grain * 6.0f);
    return Mix(dark, light, grain * 0.7f + rings * 0.3f);
}

// The props' painted metal: a coat of the color, worn through to the steel in places.
Rgb PaintedMetal(int x, int y, Rgb paint, std::uint32_t seed) {
    const float variation = Fractal(U(x), U(y), 8, 8, 4, seed);
    const float wear = Fractal(U(x), U(y), 16, 16, 3, seed + 100);
    const Rgb painted = paint * (0.85f + variation * 0.3f);
    return wear > 0.72f ? Mix(painted, Rgb{0.32f, 0.32f, 0.34f}, std::min((wear - 0.72f) * 8.0f, 1.0f)) : painted;
}

// The raised hoops around a barrel's side.
Rgb Hoops(int y, Rgb color) {
    for (const int center : {22, SideHeight - 22}) {
        const int offset = y - center;
        if (offset >= -5 && offset <= 5) {
            return color * (offset == -5 || offset == 5 ? 0.55f : 1.15f);
        }
    }
    return color;
}

// A barrel's lid: a rim, a ring pressed into it and a bung.
Rgb Lid(int x, int y, Rgb paint, std::uint32_t seed) {
    const bool top = x < static_cast<int>(Size) / 2;
    const float cx = top ? TopCapX : BottomCapX;
    const float distance = Distance(x, y, cx, CapY);
    Rgb color = PaintedMetal(x, y, paint, seed) * 0.9f;
    if (distance > CapRadius - 8.0f) {
        color = color * 0.7f;
    } else if (std::abs(distance - CapRadius * 0.62f) < 1.5f) {
        color = color * 0.6f;
    }
    const float bung = Distance(x, y, cx + CapRadius * 0.4f, CapY - CapRadius * 0.4f);
    if (top && bung < 7.0f) {
        color = bung < 4.5f ? Rgb{0.08f, 0.08f, 0.08f} : color * 1.25f;
    }
    return color;
}

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

Spot::Image UiPanel() {
    constexpr std::uint32_t PanelSize = 64;
    constexpr float Radius = 10.0f;
    const float half = static_cast<float>(PanelSize) * 0.5f;
    return PaintCoverage(PanelSize, [&](float x, float y) {
        // A rounded rectangle filling the image, half a pixel in, so its edge is smooth.
        const float dx = std::max(std::abs(x - half) - (half - 0.5f - Radius), 0.0f);
        const float dy = std::max(std::abs(y - half) - (half - 0.5f - Radius), 0.0f);
        return Coverage(std::sqrt(dx * dx + dy * dy) - Radius);
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

Spot::Image TargetDummy() {
    return Paint([](int x, int y) {
        const float radius =
            Distance(x, y, static_cast<float>(Size) * .5f, static_cast<float>(Size) * .5f) / static_cast<float>(Size);
        if (radius < .06f) {
            return Rgb{.8f, .06f, .04f};
        }
        if (radius < .43f) {
            const int ring = static_cast<int>(radius * 16);
            return ring % 2 == 0 ? Rgb{.12f, .12f, .12f} : Rgb{.9f, .87f, .78f};
        }
        return Wood(x, y, {.35f, .19f, .08f}, {.65f, .44f, .22f}, 67);
    });
}

Spot::Image Crate() {
    constexpr Rgb Dark{0.45f, 0.30f, 0.16f};
    constexpr Rgb Light{0.72f, 0.53f, 0.30f};
    constexpr int Frame = 26;
    return Paint([&](int x, int y) {
        const int size = static_cast<int>(Size);
        Rgb color = Wood(x, y, Dark, Light, 11);
        // Planks of a quarter of the side each, with a dark seam between them.
        if (y % (size / 4) < 2) {
            color = color * 0.5f;
        }
        const int edge = std::min({x, y, size - 1 - x, size - 1 - y});
        const float brace = std::abs(static_cast<float>(x - y)) / std::sqrt(2.0f);
        const bool onFrame = edge < Frame;
        const bool onBrace = !onFrame && brace < 16.0f;
        if (onFrame || onBrace) {
            color = Wood(y, x, Dark, Light, 12) * 0.85f;
            const bool outline = onFrame ? (edge < 2 || edge >= Frame - 2) : brace >= 14.0f;
            if (outline) {
                color = color * 0.45f;
            }
        }
        // Nails at the corners of the frame.
        for (const int nx : {Frame / 2, size - Frame / 2}) {
            for (const int ny : {Frame / 2, size - Frame / 2}) {
                if (Distance(x, y, static_cast<float>(nx), static_cast<float>(ny)) < 4.0f) {
                    color = Rgb{0.25f, 0.25f, 0.27f};
                }
            }
        }
        return color;
    });
}

Spot::Image Barrel() {
    constexpr Rgb Coat{0.22f, 0.36f, 0.55f};
    return Paint([&](int x, int y) {
        if (y < SideHeight) {
            return Hoops(y, PaintedMetal(x, y, Coat, 21));
        }
        return CapDistance(x, y) <= CapRadius + 2.0f ? Lid(x, y, Coat, 22) : PaintedMetal(x, y, Coat, 21);
    });
}

Spot::Image ExplosiveBarrel() {
    constexpr Rgb Coat{0.70f, 0.09f, 0.07f};
    return Paint([&](int x, int y) {
        if (y < SideHeight) {
            Rgb color = PaintedMetal(x, y, Coat, 31);
            // A band of hazard stripes around the middle.
            if (y >= 48 && y < 80) {
                const bool yellow = ((x + y) / 16) % 2 == 0;
                color = yellow ? Rgb{0.95f, 0.75f, 0.08f} : Rgb{0.08f, 0.08f, 0.08f};
                if (y == 48 || y == 79) {
                    color = Rgb{0.05f, 0.05f, 0.05f};
                }
            }
            return Hoops(y, color);
        }
        return CapDistance(x, y) <= CapRadius + 2.0f ? Lid(x, y, Coat, 32) : PaintedMetal(x, y, Coat, 31);
    });
}

Spot::Image Plank() {
    return Paint([](int x, int y) {
        Rgb color = Wood(x, y, {0.62f, 0.44f, 0.26f}, {0.84f, 0.67f, 0.44f}, 41);
        // Knots, where the grain darkens around a point.
        for (const auto [kx, ky] : {std::pair{70.0f, 90.0f}, std::pair{190.0f, 180.0f}}) {
            const float dx = (static_cast<float>(x) - kx) / 18.0f;
            const float dy = (static_cast<float>(y) - ky) / 7.0f;
            const float knot = dx * dx + dy * dy;
            if (knot < 1.0f) {
                color = color * (0.55f + knot * 0.45f);
            }
        }
        return color;
    });
}

Spot::Image Beam() {
    return Paint([](int x, int y) {
        const float brushed = Fractal(U(x), U(y), 2, 64, 3, 51);
        Rgb color = Rgb{0.42f, 0.44f, 0.47f} * (0.85f + brushed * 0.3f);
        const int size = static_cast<int>(Size);
        if (y < 4 || y >= size - 4) {
            color = color * 0.6f;
        }
        // Rivets in a row along the top and the bottom, lit from above.
        for (const int ry : {24, size - 24}) {
            for (int rx = 32; rx < size; rx += 64) {
                const float distance = Distance(x, y, static_cast<float>(rx), static_cast<float>(ry));
                if (distance < 7.0f) {
                    const float shade = static_cast<float>(ry - y) / 7.0f;
                    color = Rgb{0.55f, 0.57f, 0.6f} * (1.0f + shade * 0.3f);
                }
            }
        }
        return color;
    });
}

Spot::Image Ball() {
    constexpr Rgb Slices[] = {{0.9f, 0.15f, 0.12f}, {0.98f, 0.82f, 0.15f}, {0.15f, 0.45f, 0.9f},
                              {0.2f, 0.75f, 0.3f},  {0.98f, 0.5f, 0.1f},   {0.95f, 0.95f, 0.95f}};
    return Paint([&](int x, int y) {
        const float v = U(y);
        if (v < 0.1f || v > 0.9f) {
            return Rgb{0.96f, 0.96f, 0.96f};
        }
        const int slice = x * 6 / static_cast<int>(Size);
        return Slices[slice] * (0.92f + Fractal(U(x), v, 8, 8, 2, 61) * 0.08f);
    });
}

Spot::Image Cone() {
    return Paint([](int x, int y) {
        if (y < SideHeight) {
            // The tip is at the top of the side, the base at its bottom: two white stripes around the middle.
            const bool stripe = (y >= 44 && y < 58) || (y >= 76 && y < 88);
            const Rgb color = stripe ? Rgb{0.95f, 0.95f, 0.92f} : Rgb{1.0f, 0.42f, 0.05f};
            return color * (0.9f + Fractal(U(x), U(y), 8, 8, 3, 71) * 0.1f);
        }
        return Rgb{0.12f, 0.12f, 0.13f} * (0.9f + Fractal(U(x), U(y), 8, 8, 3, 72) * 0.2f);
    });
}

Spot::Image Concrete() {
    return Paint([](int x, int y) {
        const float coarse = Fractal(U(x), U(y), 4, 4, 5, 81);
        const float speckle = Random(x, y, 82);
        Rgb color = Rgb{0.56f, 0.56f, 0.54f} * (0.8f + coarse * 0.35f + speckle * 0.06f);
        // Grooves across, so feet do not slip.
        if (y % 32 < 3) {
            color = color * 0.72f;
        }
        return color;
    });
}

Spot::Image Wheel() {
    constexpr Rgb Rubber{0.09f, 0.09f, 0.1f};
    return Paint([&](int x, int y) {
        if (y < SideHeight) {
            // Tread blocks in chevrons around the tire.
            const int chevron = (x * 2 + std::abs(y - SideHeight / 2)) % 32;
            return Rubber * (chevron < 20 ? 1.6f : 0.8f);
        }
        const bool top = x < static_cast<int>(Size) / 2;
        const float cx = top ? TopCapX : BottomCapX;
        const float distance = Distance(x, y, cx, CapY);
        if (distance > CapRadius * 0.56f) {
            const bool ring = std::abs(distance - CapRadius * 0.8f) < 1.5f;
            return Rubber * (ring ? 0.6f : 1.3f);
        }
        // The hub: steel with five bolts around a cap.
        Rgb color = Rgb{0.62f, 0.63f, 0.66f} * (0.9f + Fractal(U(x), U(y), 16, 16, 2, 91) * 0.1f);
        if (distance < 9.0f) {
            color = color * 0.75f;
        }
        constexpr float Bolts[5][2] = {
            {0.0f, -1.0f}, {0.951f, -0.309f}, {0.588f, 0.809f}, {-0.588f, 0.809f}, {-0.951f, -0.309f}};
        for (const auto& bolt : Bolts) {
            if (Distance(x, y, cx + bolt[0] * 20.0f, CapY + bolt[1] * 20.0f) < 4.0f) {
                color = Rgb{0.2f, 0.2f, 0.22f};
            }
        }
        return color;
    });
}

namespace {

template <typename F>
SurfaceTextures PaintSurfacePoints(F&& surface) {
    return PaintSurfaceTexels(Size, [&](int x, int y, float) { return surface(x, y); });
}

// Boards along X, eight across, with gaps between them and grain along them.
SurfacePoint WoodPoint(int x, int y) {
    const int board = y / 32;
    const int inBoard = y % 32;
    const auto seed = static_cast<std::uint32_t>(900 + board);
    const float grain = Fractal(U(x), U(y), 2, 24, 3, seed);
    SurfacePoint point{.color = Wood(x, y, {0.5f, 0.33f, 0.18f}, {0.78f, 0.58f, 0.36f}, seed),
                       .height = grain * 1.5f,
                       .roughness = 0.55f + grain * 0.2f};
    if (inBoard < 2) {
        point.color = point.color * 0.45f;
        point.height = -2.0f;
        point.occlusion = 0.55f;
        point.roughness = 0.9f;
    }
    return point;
}

// Paint over steel, chipped where it wears, with the slight ripples of a sprayed coat.
SurfacePoint PaintedMetalPoint(int x, int y) {
    const float ripple = Fractal(U(x), U(y), 16, 16, 3, 910);
    const float wear = Fractal(U(x), U(y), 12, 12, 4, 911);
    SurfacePoint point{.color = Rgb{0.82f, 0.82f, 0.8f} * (0.92f + ripple * 0.1f),
                       .height = ripple * 0.6f,
                       .roughness = 0.38f + ripple * 0.1f};
    if (wear > 0.8f) {
        // The chip's edge is a step down to the steel.
        point.color = Rgb{0.62f, 0.62f, 0.64f};
        point.height = -1.0f;
        point.roughness = 0.3f;
        point.metallic = 1.0f;
    }
    return point;
}

// Steel eaten by patches of rust, which is rough, bumpy and not a metal any more.
SurfacePoint RustyMetalPoint(int x, int y) {
    const float rust = Fractal(U(x), U(y), 6, 6, 5, 920);
    const float pits = Fractal(U(x), U(y), 32, 32, 2, 921);
    const float brushed = Fractal(U(x), U(y), 2, 64, 3, 922);
    if (rust > 0.52f) {
        const float depth = std::min((rust - 0.52f) * 6.0f, 1.0f);
        return {.color = Mix(Rgb{0.55f, 0.27f, 0.12f}, Rgb{0.36f, 0.16f, 0.08f}, pits),
                .height = 1.0f + pits * 2.0f * depth,
                .occlusion = 0.85f + pits * 0.15f,
                .roughness = 0.85f + pits * 0.1f,
                .metallic = 0.0f};
    }
    return {.color = Rgb{0.6f, 0.61f, 0.63f} * (0.9f + brushed * 0.15f),
            .height = brushed * 0.4f,
            .roughness = 0.32f + brushed * 0.1f,
            .metallic = 1.0f};
}

// The props' concrete, with pores and the grooves across it.
SurfacePoint ConcretePoint(int x, int y) {
    const float coarse = Fractal(U(x), U(y), 4, 4, 5, 81);
    const float speckle = Random(x, y, 82);
    SurfacePoint point{.color = Rgb{0.56f, 0.56f, 0.54f} * (0.8f + coarse * 0.35f + speckle * 0.06f),
                       .height = coarse * 3.0f + speckle * 0.6f,
                       .roughness = 0.85f + speckle * 0.1f};
    if (y % 32 < 3) {
        point.color = point.color * 0.72f;
        point.height -= 3.0f;
        point.occlusion = 0.6f;
    }
    return point;
}

// Dark rubber with a fine grain.
SurfacePoint RubberPoint(int x, int y) {
    const float grain = Fractal(U(x), U(y), 32, 32, 3, 940);
    return {.color = Rgb{0.1f, 0.1f, 0.11f} * (0.9f + grain * 0.2f), .height = grain * 0.8f, .roughness = 0.8f};
}

// Clear glass, with faint smudges that scatter a little light and gentle waves in the pane.
SurfacePoint GlassPoint(int x, int y) {
    const float smudge = Fractal(U(x), U(y), 4, 4, 4, 950);
    const float wave = Fractal(U(x), U(y), 3, 3, 2, 951);
    return {.color = Rgb{0.95f, 0.97f, 0.98f}, .height = wave * 1.5f, .roughness = 0.04f + smudge * smudge * 0.25f};
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
    }
    return "";
}

std::uint32_t GetSurfaceSize(Surface surface) {
    return surface == Surface::Grass || surface == Surface::Rock || surface == Surface::Asphalt ||
                   surface == Surface::Slab
               ? 512
               : Size;
}

SurfaceTextures PaintSurface(Surface surface) {
    switch (surface) {
    case Surface::Wood:
        return PaintSurfacePoints(WoodPoint);
    case Surface::PaintedMetal:
        return PaintSurfacePoints(PaintedMetalPoint);
    case Surface::RustyMetal:
        return PaintSurfacePoints(RustyMetalPoint);
    case Surface::Concrete:
        return PaintSurfacePoints(ConcretePoint);
    case Surface::Rubber:
        return PaintSurfacePoints(RubberPoint);
    case Surface::Glass:
        return PaintSurfacePoints(GlassPoint);
    default:
        break;
    }
    return PaintLevelSurface(surface, GetSurfaceSize(surface));
}

} // namespace Generator::Textures
