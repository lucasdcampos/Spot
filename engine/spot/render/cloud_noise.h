#pragma once

// Engine-internal: the noise the sky's clouds are shaped from (render/shaders/clouds.frag.hlsl).

#include <cstdint>
#include <span>

namespace Spot::Detail {

// The side of the square texture of cloud noise, in texels.
inline constexpr std::uint32_t CloudNoiseSize = 256;
// How many clouds, about, the shapes in red repeat after across the texture.
inline constexpr std::uint32_t CloudNoiseTile = 8;

// Noise the clouds are shaped from, as Rgba8 texels row after row, the same every time; it is made the first time a
// process asks for it. Every channel repeats across the texture, so it tiles, and spreads evenly from 0 to 1: the
// share of its texels above 1 - c is c, which makes a coverage of c cover that share of the sky.
//   red    the clouds' shapes: smooth noise, lumpy with cells of about a cloud each and finer
//   green  billows: cells finer than the shapes, which carve their edges into rounded lumps
//   blue   wisps: finer smooth noise, which frays the edges of soft clouds
//   alpha  weather: smooth noise far wider than a cloud, where clouds gather or thin out
std::span<const std::uint8_t> GetCloudNoise();

// The side of the cube of volume noise, in texels.
inline constexpr std::uint32_t CloudVolumeSize = 32;

// Noise that carves the volume of clouds, as a cube of Rgba8 texels, slice after slice of rows, made the first time a
// process asks for it. Like GetCloudNoise(), each channel tiles along every axis and spreads evenly from 0 to 1.
//   red    billows: rounded cells, four across the cube, and smooth noise between them
//   green  finer billows, which fray what the red ones leave
//   blue   swells: smooth noise, two waves across the cube and finer, which raise towers and hollow out valleys
std::span<const std::uint8_t> GetCloudVolumeNoise();

} // namespace Spot::Detail
