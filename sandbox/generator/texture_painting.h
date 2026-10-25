#pragma once

#include "textures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

// What the generator paints textures with: colors, noise that tiles, and surfaces made into their PBR textures. Only
// arithmetic and square roots are used, which give the same results everywhere, so every machine paints the same
// pixels.
namespace Generator::Textures {

// Colors as they are displayed (sRGB), from 0 to 1.
struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

inline Rgb operator*(Rgb color, float scale) {
    return {color.r * scale, color.g * scale, color.b * scale};
}

inline Rgb Mix(Rgb a, Rgb b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

inline std::uint32_t Hash(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
    std::uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    h *= 0xc2b2ae35u;
    h ^= h >> 15;
    return h;
}

// From 0 to 1.
inline float Random(int x, int y, std::uint32_t seed) {
    return static_cast<float>(Hash(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed) >> 8) /
           16777216.0f;
}

// The texture's coordinates, from 0 to 1, at the pixel's center, for a texture of the size given by the scale of a
// 256-pixel one's pixels, as PaintPoints() gives it.
inline float Coordinate(int pixel, float scale) {
    return (static_cast<float>(pixel) + 0.5f) * scale / 256.0f;
}

inline int Wrap(int value, int period) {
    return ((value % period) + period) % period;
}

// Smooth noise from 0 to 1 over a lattice of the periods, so it tiles every period cells.
inline float ValueNoise(float x, float y, int periodX, int periodY, std::uint32_t seed) {
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
inline float Fractal(float u, float v, int cellsX, int cellsY, int octaves, std::uint32_t seed) {
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

// Unit directions a sixteenth of a turn apart, written out so no machine works them out differently.
inline constexpr float GradientDirections[16][2] = {
    {1.0f, 0.0f},  {0.92388f, 0.38268f},   {0.70711f, 0.70711f},   {0.38268f, 0.92388f},
    {0.0f, 1.0f},  {-0.38268f, 0.92388f},  {-0.70711f, 0.70711f},  {-0.92388f, 0.38268f},
    {-1.0f, 0.0f}, {-0.92388f, -0.38268f}, {-0.70711f, -0.70711f}, {-0.38268f, -0.92388f},
    {0.0f, -1.0f}, {0.38268f, -0.92388f},  {0.70711f, -0.70711f},  {0.92388f, -0.38268f}};

// Gradient noise from 0 to 1, mostly near the middle, over a lattice of the periods, so it tiles every period cells.
// Unlike ValueNoise, its features are round rather than lined up with the lattice, so it holds up seen close.
inline float GradientNoise(float x, float y, int periodX, int periodY, std::uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const float tx = x - fx;
    const float ty = y - fy;
    const auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const auto at = [&](int dx, int dy) {
        const std::uint32_t h = Hash(static_cast<std::uint32_t>(Wrap(x0 + dx, periodX)),
                                     static_cast<std::uint32_t>(Wrap(y0 + dy, periodY)), seed);
        const float* direction = GradientDirections[h >> 28];
        return direction[0] * (tx - static_cast<float>(dx)) + direction[1] * (ty - static_cast<float>(dy));
    };
    const float sx = fade(tx);
    const float sy = fade(ty);
    const float top = at(0, 0) + (at(1, 0) - at(0, 0)) * sx;
    const float bottom = at(0, 1) + (at(1, 1) - at(0, 1)) * sx;
    return std::clamp((top + (bottom - top) * sy) * 0.7071f + 0.5f, 0.0f, 1.0f);
}

// Gradient noise in octaves, as Fractal sums value noise, stretched from 0 to 1 so its usual values fill the range.
inline float Noise(float u, float v, int cellsX, int cellsY, int octaves, std::uint32_t seed) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        const int scale = 1 << octave;
        sum += (GradientNoise(u * static_cast<float>(cellsX * scale), v * static_cast<float>(cellsY * scale),
                              cellsX * scale, cellsY * scale, seed + static_cast<std::uint32_t>(octave)) -
                0.5f) *
               amplitude;
        total += amplitude;
        amplitude *= 0.5f;
    }
    return std::clamp(sum / total * 2.2f + 0.5f, 0.0f, 1.0f);
}

// Sharp crests where the noise crosses its middle, from 0 to 1, as of folds, scratches and cracks.
inline float Ridges(float u, float v, int cellsX, int cellsY, int octaves, std::uint32_t seed) {
    return 1.0f - std::abs(Noise(u, v, cellsX, cellsY, octaves, seed) * 2.0f - 1.0f);
}

// Cells around points scattered one per lattice cell, tiling every period cells: how far the pixel is from the nearest
// point and the second nearest, in cells, and a number from 0 to 1 that tells the nearest point's cell from the others.
struct CellSample {
    float nearest = 0.0f;
    float second = 0.0f;
    float id = 0.0f;
};

// Cells tiling every periodX cells along x and periodY along y, so stretching x and y differently stretches them.
inline CellSample Cells(float x, float y, int periodX, int periodY, std::uint32_t seed) {
    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));
    CellSample sample{1e9f, 1e9f, 0.0f};
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int px = cx + dx;
            const int py = cy + dy;
            const int wx = Wrap(px, periodX);
            const int wy = Wrap(py, periodY);
            const float fx = static_cast<float>(px) + Random(wx, wy, seed);
            const float fy = static_cast<float>(py) + Random(wx, wy, seed + 1);
            const float distance = std::sqrt((x - fx) * (x - fx) + (y - fy) * (y - fy));
            if (distance < sample.nearest) {
                sample.second = sample.nearest;
                sample.nearest = distance;
                sample.id = Random(wx, wy, seed + 2);
            } else if (distance < sample.second) {
                sample.second = distance;
            }
        }
    }
    return sample;
}

inline CellSample Cells(float x, float y, int period, std::uint32_t seed) {
    return Cells(x, y, period, period, seed);
}

// A smooth wave of the phase, in turns, from -1 to 1, shaped like a sine from arithmetic alone.
inline float Wave(float phase) {
    const float x = phase - std::floor(phase);
    const float t = x < 0.5f ? x * 4.0f - 1.0f : 3.0f - x * 4.0f;
    const float y = (1.0f - t * t) * (x < 0.5f ? 1.0f : -1.0f);
    return y * (0.775f + 0.225f * std::abs(y));
}

inline float Smooth(float from, float to, float value) {
    const float t = std::clamp((value - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Wood's figure at a point of a board whose grain runs along u: the growth rings, rings times across the texture
// along v so it tiles, wandering and arching as a board sawn from a log shows them, with the dark latewood of each
// year and the pores and fibers along the grain. Knots bend the rings around them.
struct WoodFigure {
    // From 0 for pale earlywood to 1 for latewood, the pores' streaks from 0 to 1, and fine fibers from 0 to 1.
    float tone = 0.0f;
    float pores = 0.0f;
    float fibers = 0.0f;
};

struct Knot {
    float u = 0.0f;
    float v = 0.0f;
    // Its size along and across the grain, in the texture's units.
    float length = 0.0f;
    float width = 0.0f;
};

inline WoodFigure WoodGrain(float u, float v, int rings, std::uint32_t seed, std::initializer_list<Knot> knots = {}) {
    // The rings drift slowly across the board along it, rather than waving, as a board sawn along a straight log's.
    float phase =
        v * static_cast<float>(rings) + Noise(u, v, 1, 2, 2, seed) * 1.6f + Noise(u, v, 3, 2, 2, seed + 4) * 0.35f;
    float knotted = 0.0f;
    for (const Knot& knot : knots) {
        const float du = (u - knot.u) / knot.length;
        const float dv = (v - knot.v) / knot.width;
        const float closeness = 1.0f / (1.0f + du * du + dv * dv);
        // The rings swell around the knot, and its heart is dark wood of its own.
        phase += closeness * closeness * 3.0f * (dv < 0.0f ? -1.0f : 1.0f);
        knotted = std::max(knotted, Smooth(0.45f, 0.8f, closeness));
    }
    const float ring = phase - std::floor(phase);
    const float late = Smooth(0.62f, 0.9f, ring) * (1.0f - Smooth(0.92f, 1.0f, ring));
    const float pores = Smooth(0.6f, 0.85f, Noise(u, v, 8, 192, 2, seed + 8));
    const float fibers = Noise(u, v, 16, 320, 2, seed + 12);
    return {.tone = std::min(late * 0.75f + fibers * 0.25f + knotted * 0.8f, 1.0f), .pores = pores, .fibers = fibers};
}

// What a surface is at a pixel: its color, its height (in pixels, for its normals), how much light reaches it, its
// roughness and metalness, and how much of the pixel it covers, for cutouts such as leaves.
struct SurfacePoint {
    Rgb color;
    float height = 0.0f;
    float occlusion = 1.0f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float alpha = 1.0f;
};

// Encodes a surface's points, size pixels square, row by row, into its textures; normals come from the slopes of the
// heights, which wrap around the edges so they tile.
inline SurfaceTextures EncodeSurface(std::uint32_t size, const std::vector<SurfacePoint>& points);

// What time does to a surface, by how far each pixel stands above or below the surface around it: dirt gathers in
// hollows, darkening and shading them, and handling wears the edges that stand out lighter.
struct Weathering {
    // How far around each pixel the surface is compared with it, and how much lower or higher it must be for the full
    // effect, both in the pixels of a 256-pixel texture.
    float radius = 3.0f;
    float depth = 2.0f;
    // From 0 to 1: how dark full hollows get, how much less light reaches them, and how much lighter full edges get.
    float dirt = 0.0f;
    float occlusion = 0.0f;
    float wear = 0.0f;
};

namespace Detail {

// Averages the values, size pixels square, over a square around each, the radius in pixels, wrapping; twice over,
// so it is smooth.
inline void BlurWrapped(std::vector<float>& values, int size, int radius) {
    std::vector<float> line(static_cast<std::size_t>(size));
    const float count = static_cast<float>(radius * 2 + 1);
    for (int pass = 0; pass < 4; ++pass) {
        // Along the rows, then down the columns, then both again.
        const bool rows = pass % 2 == 0;
        for (int a = 0; a < size; ++a) {
            const auto at = [&](int b) -> float& {
                const auto wrapped = static_cast<std::size_t>(Wrap(b, size));
                const auto other = static_cast<std::size_t>(a);
                const auto stride = static_cast<std::size_t>(size);
                return rows ? values[other * stride + wrapped] : values[wrapped * stride + other];
            };
            float sum = 0.0f;
            for (int b = -radius; b <= radius; ++b) {
                sum += at(b);
            }
            for (int b = 0; b < size; ++b) {
                line[static_cast<std::size_t>(b)] = sum / count;
                sum += at(b + radius + 1) - at(b - radius);
            }
            for (int b = 0; b < size; ++b) {
                at(b) = line[static_cast<std::size_t>(b)];
            }
        }
    }
}

} // namespace Detail

// Weathers the points, size pixels square, whose heights are still in the pixels of a 256-pixel texture, each of
// which is scale of theirs.
inline void Weather(std::vector<SurfacePoint>& points, std::uint32_t size, float scale, const Weathering& weathering) {
    std::vector<float> around(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        around[i] = points[i].height;
    }
    Detail::BlurWrapped(around, static_cast<int>(size),
                        std::max(static_cast<int>(weathering.radius / scale * 0.5f + 0.5f), 1));
    for (std::size_t i = 0; i < points.size(); ++i) {
        SurfacePoint& point = points[i];
        const float relief = std::clamp((point.height - around[i]) / weathering.depth, -1.0f, 1.0f);
        const float hollow = std::max(-relief, 0.0f);
        const float edge = std::max(relief, 0.0f);
        point.color = point.color * ((1.0f - hollow * weathering.dirt) * (1.0f + edge * weathering.wear));
        point.occlusion *= 1.0f - hollow * weathering.occlusion;
        point.roughness = std::min(point.roughness + hollow * weathering.dirt * 0.25f, 1.0f);
    }
}

// What a surface is at each pixel, size pixels square, row by row: the function takes the pixel and how many pixels
// of a 256-pixel texture each covers, so heights can be given in those pixels whatever the size, as they are kept.
template <typename F>
std::vector<SurfacePoint> PaintPoints(std::uint32_t size, F&& surface) {
    const float scale = 256.0f / static_cast<float>(size);
    std::vector<SurfacePoint> points(static_cast<std::size_t>(size) * size);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            points[static_cast<std::size_t>(y) * size + x] = surface(static_cast<int>(x), static_cast<int>(y), scale);
        }
    }
    return points;
}

// Encodes points painted by PaintPoints(), weathered by each of the weatherings in turn.
inline SurfaceTextures EncodePainted(std::uint32_t size, std::vector<SurfacePoint> points,
                                     std::initializer_list<Weathering> weatherings = {}) {
    const float scale = 256.0f / static_cast<float>(size);
    for (const Weathering& weathering : weatherings) {
        Weather(points, size, scale, weathering);
    }
    for (SurfacePoint& point : points) {
        point.height /= scale;
    }
    return EncodeSurface(size, points);
}

// Paints a surface's textures, size pixels square, from what it is at each pixel, as PaintPoints() takes it, then
// weathers it by each of the weatherings in turn.
template <typename F>
SurfaceTextures PaintSurfaceTexels(std::uint32_t size, F&& surface,
                                   std::initializer_list<Weathering> weatherings = {}) {
    return EncodePainted(size, PaintPoints(size, std::forward<F>(surface)), weatherings);
}

inline SurfaceTextures EncodeSurface(std::uint32_t size, const std::vector<SurfacePoint>& points) {
    const auto at = [&](int x, int y) -> const SurfacePoint& {
        const int wrap = static_cast<int>(size);
        return points[static_cast<std::size_t>(Wrap(y, wrap)) * size + static_cast<std::size_t>(Wrap(x, wrap))];
    };
    const auto byte = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    std::vector<std::uint8_t> color(points.size() * 4);
    std::vector<std::uint8_t> normal(points.size() * 4);
    std::vector<std::uint8_t> orm(points.size() * 4);
    for (int y = 0; y < static_cast<int>(size); ++y) {
        for (int x = 0; x < static_cast<int>(size); ++x) {
            const SurfacePoint& point = at(x, y);
            const std::size_t i = (static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 4;
            color[i] = byte(point.color.r);
            color[i + 1] = byte(point.color.g);
            color[i + 2] = byte(point.color.b);
            color[i + 3] = byte(point.alpha);
            // Slopes across the pixels around it, wrapping, so the normals tile; up the texture is toward lower y.
            const float dx = (at(x + 1, y).height - at(x - 1, y).height) * 0.5f;
            const float dy = (at(x, y - 1).height - at(x, y + 1).height) * 0.5f;
            const float length = std::sqrt(dx * dx + dy * dy + 1.0f);
            normal[i] = byte(-dx / length * 0.5f + 0.5f);
            normal[i + 1] = byte(-dy / length * 0.5f + 0.5f);
            normal[i + 2] = byte(1.0f / length * 0.5f + 0.5f);
            normal[i + 3] = 255;
            orm[i] = byte(point.occlusion);
            orm[i + 1] = byte(point.roughness);
            orm[i + 2] = byte(point.metallic);
            orm[i + 3] = 255;
        }
    }
    return {Spot::Image(size, size, std::move(color)), Spot::Image(size, size, std::move(normal)),
            Spot::Image(size, size, std::move(orm))};
}

} // namespace Generator::Textures
