#pragma once

#include "spot/graphics/camera_3d.h"
#include "spot/math/vector.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Spot::Detail {

// The view is cut into a grid of clusters, tiles of the screen by slices of depth, and each cluster lists the point
// and spot lights that may reach it, so each pixel only goes through the lights near it.
inline constexpr std::uint32_t ClusterTilesX = 16;
inline constexpr std::uint32_t ClusterTilesY = 9;
inline constexpr std::uint32_t ClusterSlices = 24;
inline constexpr std::uint32_t ClusterCount = ClusterTilesX * ClusterTilesY * ClusterSlices;
// Slices stop growing past this depth; what is beyond is in the last one.
inline constexpr float MaxClusterDepth = 500.0f;

// A light with a position and a range, by its index among the lights the shader reads.
struct ClusterLight {
    Vector3 position;
    float range = 0.0f;
    std::uint32_t index = 0;
};

struct LightClusters {
    // Per cluster, the tiles across first, then down, then the slices: the first of its lights in lightIndices, and
    // how many there are.
    std::vector<std::array<std::uint32_t, 2>> clusters;
    std::vector<std::uint32_t> lightIndices;
    // Slices are even in the logarithm of depth, from the near plane: slice = log(depth / near) * sliceScale.
    float nearDepth = 0.1f;
    float sliceScale = 1.0f;
};

// The slice of a depth along the camera's forward axis.
std::uint32_t GetClusterSlice(const LightClusters& clusters, float depth);
std::uint32_t GetClusterIndex(std::uint32_t tileX, std::uint32_t tileY, std::uint32_t slice);

// Puts each light in every cluster its sphere may reach. The tiles a light covers come from its sphere's box in view
// space, projected, so they may include a few it misses, never miss one it reaches.
LightClusters BuildLightClusters(const Camera3D& camera, Vector2 viewSize, std::span<const ClusterLight> lights);

} // namespace Spot::Detail
