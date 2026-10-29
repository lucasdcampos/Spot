#include "spot/render/cloud_noise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace Spot::Detail {

namespace {

constexpr int Size = static_cast<int>(CloudNoiseSize);

std::uint32_t Hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// A number from 0 to 1 for a cell of a grid, a different one for each seed.
float Random(int x, int y, std::uint32_t seed) {
    const std::uint32_t h = Hash(static_cast<std::uint32_t>(x) + Hash(static_cast<std::uint32_t>(y) + Hash(seed)));
    return static_cast<float>(h >> 8) / 16777215.0f;
}

// A grid of period by period cells over the texture, with two numbers for each cell: a direction for gradient noise,
// or a point inside the cell for cellular noise. It wraps around the texture's edges, so the noise made from it tiles.
struct Grid {
    int period;
    std::vector<std::array<float, 2>> values;

    const std::array<float, 2>& At(int x, int y) const {
        x = (x % period + period) % period;
        y = (y % period + period) % period;
        return values[static_cast<std::size_t>(y * period + x)];
    }
};

Grid MakeGrid(int period, std::uint32_t seed, bool directions) {
    Grid grid{period, std::vector<std::array<float, 2>>(static_cast<std::size_t>(period * period))};
    for (int y = 0; y < period; ++y) {
        for (int x = 0; x < period; ++x) {
            auto& value = grid.values[static_cast<std::size_t>(y * period + x)];
            if (directions) {
                const float angle = Random(x, y, seed) * 2.0f * std::numbers::pi_v<float>;
                value = {std::cos(angle), std::sin(angle)};
            } else {
                value = {Random(x, y, seed), Random(x, y, seed + 1000)};
            }
        }
    }
    return grid;
}

// Where the texel's center lies on a grid of the period, in cells.
float ToCells(int texel, int period) {
    return (static_cast<float>(texel) + 0.5f) * static_cast<float>(period) / static_cast<float>(Size);
}

float Fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Gradient (Perlin) noise, from about -0.7 to 0.7.
float Perlin(const Grid& grid, int x, int y) {
    const float px = ToCells(x, grid.period);
    const float py = ToCells(y, grid.period);
    const float cellX = std::floor(px);
    const float cellY = std::floor(py);
    const float fx = px - cellX;
    const float fy = py - cellY;
    const auto corner = [&](int dx, int dy) {
        const auto& direction = grid.At(static_cast<int>(cellX) + dx, static_cast<int>(cellY) + dy);
        return direction[0] * (fx - static_cast<float>(dx)) + direction[1] * (fy - static_cast<float>(dy));
    };
    const float u = Fade(fx);
    return std::lerp(std::lerp(corner(0, 0), corner(1, 0), u), std::lerp(corner(0, 1), corner(1, 1), u), Fade(fy));
}

// How close the texel is to the nearest of points scattered one in each cell (inverted Worley noise): 1 on a point,
// falling off in rounded mounds, 0 a cell or more away.
float Worley(const Grid& grid, int x, int y) {
    const float px = ToCells(x, grid.period);
    const float py = ToCells(y, grid.period);
    const float cellX = std::floor(px);
    const float cellY = std::floor(py);
    float nearest = 1.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto& point = grid.At(static_cast<int>(cellX) + dx, static_cast<int>(cellY) + dy);
            const float ox = cellX + static_cast<float>(dx) + point[0] - px;
            const float oy = cellY + static_cast<float>(dy) + point[1] - py;
            nearest = std::min(nearest, ox * ox + oy * oy);
        }
    }
    return 1.0f - std::sqrt(nearest);
}

// Replaces each value by the share of the values below it, so they keep their order and spread evenly from 0 to 1.
void Equalize(std::vector<float>& values) {
    constexpr std::size_t Bins = 4096;
    const auto [low, high] = std::ranges::minmax(values);
    const float scale = static_cast<float>(Bins - 1) / std::max(high - low, 1e-6f);
    const auto bin = [&](float value) { return static_cast<std::size_t>((value - low) * scale); };
    std::vector<std::uint32_t> counts(Bins);
    for (const float value : values) {
        ++counts[bin(value)];
    }
    std::vector<float> shares(Bins);
    std::uint32_t below = 0;
    for (std::size_t i = 0; i < Bins; ++i) {
        shares[i] =
            (static_cast<float>(below) + static_cast<float>(counts[i]) * 0.5f) / static_cast<float>(values.size());
        below += counts[i];
    }
    for (float& value : values) {
        value = shares[bin(value)];
    }
}

std::vector<std::uint8_t> MakeCloudNoise() {
    constexpr auto Tile = static_cast<int>(CloudNoiseTile);
    // Each octave twice as fine as the one before; the shapes' first are about a cloud wide.
    const std::array shapes{MakeGrid(Tile, 1, true), MakeGrid(Tile * 2, 2, true), MakeGrid(Tile * 4, 3, true)};
    const std::array cells{MakeGrid(Tile, 4, false), MakeGrid(Tile * 2, 5, false), MakeGrid(Tile * 4, 14, false)};
    const std::array billows{MakeGrid(Tile, 6, false), MakeGrid(Tile * 2, 7, false), MakeGrid(Tile * 4, 8, false)};
    const std::array wisps{MakeGrid(Tile, 9, true), MakeGrid(Tile * 2, 10, true), MakeGrid(Tile * 4, 11, true)};
    const std::array weather{MakeGrid(2, 12, true), MakeGrid(4, 13, true)};

    constexpr auto Texels = static_cast<std::size_t>(Size * Size);
    std::array<std::vector<float>, 4> channels;
    for (auto& channel : channels) {
        channel.resize(Texels);
    }
    for (int y = 0; y < Size; ++y) {
        for (int x = 0; x < Size; ++x) {
            const auto i = static_cast<std::size_t>(y * Size + x);
            const float smooth =
                Perlin(shapes[0], x, y) + 0.5f * Perlin(shapes[1], x, y) + 0.25f * Perlin(shapes[2], x, y);
            const float round = Worley(cells[0], x, y) + 0.5f * Worley(cells[1], x, y) + 0.25f * Worley(cells[2], x, y);
            // Mostly the smooth noise, for shapes of every kind, made lumpy at their edges by the cells.
            channels[0][i] = smooth + 0.45f * round;
            channels[1][i] =
                Worley(billows[0], x, y) + 0.5f * Worley(billows[1], x, y) + 0.25f * Worley(billows[2], x, y);
            channels[2][i] = Perlin(wisps[0], x, y) + 0.5f * Perlin(wisps[1], x, y) + 0.25f * Perlin(wisps[2], x, y);
            channels[3][i] = Perlin(weather[0], x, y) + 0.5f * Perlin(weather[1], x, y);
        }
    }

    std::vector<std::uint8_t> texels(Texels * 4);
    for (std::size_t c = 0; c < channels.size(); ++c) {
        Equalize(channels[c]);
        for (std::size_t i = 0; i < Texels; ++i) {
            texels[i * 4 + c] = static_cast<std::uint8_t>(std::lround(channels[c][i] * 255.0f));
        }
    }
    return texels;
}

constexpr int VolumeSize = static_cast<int>(CloudVolumeSize);

// A grid of period cells along each axis of the volume, wrapping around it, with three numbers for each cell: a
// direction for gradient noise, or a point inside the cell for cellular noise.
struct VolumeGrid {
    int period;
    std::vector<std::array<float, 3>> values;

    const std::array<float, 3>& At(int x, int y, int z) const {
        x = (x % period + period) % period;
        y = (y % period + period) % period;
        z = (z % period + period) % period;
        return values[static_cast<std::size_t>((z * period + y) * period + x)];
    }
};

VolumeGrid MakeVolumeGrid(int period, std::uint32_t seed, bool directions) {
    VolumeGrid grid{period, std::vector<std::array<float, 3>>(static_cast<std::size_t>(period * period * period))};
    for (int z = 0; z < period; ++z) {
        for (int y = 0; y < period; ++y) {
            for (int x = 0; x < period; ++x) {
                const float a = Random(x + z * 7919, y, seed);
                const float b = Random(x + z * 7919, y, seed + 1000);
                const float c = Random(x + z * 7919, y, seed + 2000);
                auto& value = grid.values[static_cast<std::size_t>((z * period + y) * period + x)];
                if (directions) {
                    // Spread evenly over the sphere.
                    const float up = a * 2.0f - 1.0f;
                    const float around = b * 2.0f * std::numbers::pi_v<float>;
                    const float across = std::sqrt(1.0f - up * up);
                    value = {across * std::cos(around), across * std::sin(around), up};
                } else {
                    value = {a, b, c};
                }
            }
        }
    }
    return grid;
}

float ToVolumeCells(int texel, int period) {
    return (static_cast<float>(texel) + 0.5f) * static_cast<float>(period) / static_cast<float>(VolumeSize);
}

float Perlin(const VolumeGrid& grid, int x, int y, int z) {
    const std::array p{ToVolumeCells(x, grid.period), ToVolumeCells(y, grid.period), ToVolumeCells(z, grid.period)};
    const std::array cell{std::floor(p[0]), std::floor(p[1]), std::floor(p[2])};
    const std::array f{p[0] - cell[0], p[1] - cell[1], p[2] - cell[2]};
    const auto corner = [&](int dx, int dy, int dz) {
        const auto& direction =
            grid.At(static_cast<int>(cell[0]) + dx, static_cast<int>(cell[1]) + dy, static_cast<int>(cell[2]) + dz);
        return direction[0] * (f[0] - static_cast<float>(dx)) + direction[1] * (f[1] - static_cast<float>(dy)) +
               direction[2] * (f[2] - static_cast<float>(dz));
    };
    const float u = Fade(f[0]);
    const float v = Fade(f[1]);
    const auto plane = [&](int dz) {
        return std::lerp(std::lerp(corner(0, 0, dz), corner(1, 0, dz), u),
                         std::lerp(corner(0, 1, dz), corner(1, 1, dz), u), v);
    };
    return std::lerp(plane(0), plane(1), Fade(f[2]));
}

float Worley(const VolumeGrid& grid, int x, int y, int z) {
    const std::array p{ToVolumeCells(x, grid.period), ToVolumeCells(y, grid.period), ToVolumeCells(z, grid.period)};
    const std::array cell{std::floor(p[0]), std::floor(p[1]), std::floor(p[2])};
    float nearest = 1.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const auto& point = grid.At(static_cast<int>(cell[0]) + dx, static_cast<int>(cell[1]) + dy,
                                            static_cast<int>(cell[2]) + dz);
                const float ox = cell[0] + static_cast<float>(dx) + point[0] - p[0];
                const float oy = cell[1] + static_cast<float>(dy) + point[1] - p[1];
                const float oz = cell[2] + static_cast<float>(dz) + point[2] - p[2];
                nearest = std::min(nearest, ox * ox + oy * oy + oz * oz);
            }
        }
    }
    return 1.0f - std::sqrt(nearest);
}

std::vector<std::uint8_t> MakeCloudVolumeNoise() {
    const std::array smooth{MakeVolumeGrid(4, 21, true), MakeVolumeGrid(8, 22, true)};
    const std::array cells{MakeVolumeGrid(4, 23, false), MakeVolumeGrid(8, 24, false)};
    const std::array billows{MakeVolumeGrid(8, 25, false), MakeVolumeGrid(16, 26, false)};
    const std::array swells{MakeVolumeGrid(2, 27, true), MakeVolumeGrid(4, 28, true), MakeVolumeGrid(8, 29, true)};

    constexpr auto Texels = static_cast<std::size_t>(VolumeSize * VolumeSize * VolumeSize);
    std::array<std::vector<float>, 3> channels;
    for (auto& channel : channels) {
        channel.resize(Texels);
    }
    for (int z = 0; z < VolumeSize; ++z) {
        for (int y = 0; y < VolumeSize; ++y) {
            for (int x = 0; x < VolumeSize; ++x) {
                const auto i = static_cast<std::size_t>((z * VolumeSize + y) * VolumeSize + x);
                const float round = Worley(cells[0], x, y, z) + 0.5f * Worley(cells[1], x, y, z);
                channels[0][i] = 0.5f * (Perlin(smooth[0], x, y, z) + 0.5f * Perlin(smooth[1], x, y, z)) + round;
                channels[1][i] = Worley(billows[0], x, y, z) + 0.5f * Worley(billows[1], x, y, z);
                channels[2][i] = Perlin(swells[0], x, y, z) + 0.5f * Perlin(swells[1], x, y, z) +
                                 0.25f * Perlin(swells[2], x, y, z);
            }
        }
    }

    std::vector<std::uint8_t> texels(Texels * 4, 255);
    for (std::size_t c = 0; c < channels.size(); ++c) {
        Equalize(channels[c]);
        for (std::size_t i = 0; i < Texels; ++i) {
            texels[i * 4 + c] = static_cast<std::uint8_t>(std::lround(channels[c][i] * 255.0f));
        }
    }
    return texels;
}

} // namespace

std::span<const std::uint8_t> GetCloudNoise() {
    static const std::vector<std::uint8_t> noise = MakeCloudNoise();
    return noise;
}

std::span<const std::uint8_t> GetCloudVolumeNoise() {
    static const std::vector<std::uint8_t> noise = MakeCloudVolumeNoise();
    return noise;
}

} // namespace Spot::Detail
