#pragma once

#include "textures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// Cells around points scattered one per lattice cell, tiling every period cells: how far the pixel is from the nearest
// point and the second nearest, in cells, and a number from 0 to 1 that tells the nearest point's cell from the others.
struct CellSample {
    float nearest = 0.0f;
    float second = 0.0f;
    float id = 0.0f;
};

inline CellSample Cells(float x, float y, int period, std::uint32_t seed) {
    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));
    CellSample sample{1e9f, 1e9f, 0.0f};
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int px = cx + dx;
            const int py = cy + dy;
            const int wx = Wrap(px, period);
            const int wy = Wrap(py, period);
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

// What a surface is at a pixel: its color, its height (in pixels, for its normals), how much light reaches it, and its
// roughness and metalness.
struct SurfacePoint {
    Rgb color;
    float height = 0.0f;
    float occlusion = 1.0f;
    float roughness = 0.5f;
    float metallic = 0.0f;
};

// Paints a surface's textures, size pixels square, from what it is at each pixel: the function takes the pixel and
// how many pixels of a 256-pixel texture each covers, so heights can be given in those pixels whatever the size.
template <typename F>
SurfaceTextures PaintSurfaceTexels(std::uint32_t size, F&& surface) {
    const float scale = 256.0f / static_cast<float>(size);
    std::vector<SurfacePoint> points(static_cast<std::size_t>(size) * size);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            SurfacePoint point = surface(static_cast<int>(x), static_cast<int>(y), scale);
            point.height /= scale;
            points[static_cast<std::size_t>(y) * size + x] = point;
        }
    }
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
            color[i + 3] = 255;
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
