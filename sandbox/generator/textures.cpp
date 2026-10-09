#include "textures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace Generator::Textures {

namespace {

// Colors as they are displayed (sRGB), from 0 to 1. Only arithmetic and square roots paint them, which give the same
// results everywhere, so the textures are the same on every machine.
struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

Rgb operator*(Rgb color, float scale) {
    return {color.r * scale, color.g * scale, color.b * scale};
}

Rgb Mix(Rgb a, Rgb b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

std::uint32_t Hash(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
    std::uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    h *= 0xc2b2ae35u;
    h ^= h >> 15;
    return h;
}

// From 0 to 1.
float Random(int x, int y, std::uint32_t seed) {
    return static_cast<float>(Hash(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed) >> 8) /
           16777216.0f;
}

int Wrap(int value, int period) {
    return ((value % period) + period) % period;
}

// Smooth noise from 0 to 1 over a lattice of the periods, so it tiles every period cells.
float ValueNoise(float x, float y, int periodX, int periodY, std::uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const auto fade = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float tx = fade(x - fx);
    const float ty = fade(y - fy);
    const auto at = [&](int dx, int dy) { return Random(Wrap(x0 + dx, periodX), Wrap(y0 + dy, periodY), seed); };
    const float top = at(0, 0) + (at(1, 0) - at(0, 0)) * tx;
    const float bottom = at(0, 1) + (at(1, 1) - at(0, 1)) * tx;
    return top + (bottom - top) * ty;
}

// Noise in octaves, each twice as fine and half as strong, from 0 to 1. The texture is cellsX by cellsY cells of the
// first octave, so it tiles.
float Fractal(float u, float v, int cellsX, int cellsY, int octaves, std::uint32_t seed) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        const int scale = 1 << octave;
        sum += ValueNoise(u * static_cast<float>(cellsX * scale), v * static_cast<float>(cellsY * scale),
                          cellsX * scale, cellsY * scale, seed + static_cast<std::uint32_t>(octave)) *
               amplitude;
        total += amplitude;
        amplitude *= 0.5f;
    }
    return sum / total;
}

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

} // namespace

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

} // namespace Generator::Textures
