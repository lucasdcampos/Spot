#include "graphics/light_clusters.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Spot::Detail {

std::uint32_t GetClusterSlice(const LightClusters& clusters, float depth) {
    if (depth <= clusters.nearDepth) {
        return 0;
    }
    const float slice = std::floor(std::log(depth / clusters.nearDepth) * clusters.sliceScale);
    return static_cast<std::uint32_t>(std::clamp(slice, 0.0f, static_cast<float>(ClusterSlices - 1)));
}

std::uint32_t GetClusterIndex(std::uint32_t tileX, std::uint32_t tileY, std::uint32_t slice) {
    return (slice * ClusterTilesY + tileY) * ClusterTilesX + tileX;
}

LightClusters BuildLightClusters(const Camera3D& camera, Vector2 viewSize, std::span<const ClusterLight> lights) {
    LightClusters result;
    result.nearDepth = std::max(camera.nearPlane, 0.001f);
    const float farDepth = std::clamp(camera.farPlane, result.nearDepth * 2.0f, MaxClusterDepth);
    result.sliceScale = static_cast<float>(ClusterSlices) / std::log(farDepth / result.nearDepth);

    // The clusters each light covers, as ranges of tiles and slices: counted first, then laid out one cluster after
    // the other.
    struct Cover {
        std::uint32_t index;
        std::uint32_t firstX, lastX, firstY, lastY, firstSlice, lastSlice;
    };
    std::vector<Cover> covers;
    covers.reserve(lights.size());
    const Matrix4 view = camera.GetView();
    const Matrix4 projection = camera.GetProjection(viewSize);
    for (const ClusterLight& light : lights) {
        const Vector4 center = view * Vector4{light.position.x, light.position.y, light.position.z, 1.0f};
        const float radius = light.range;
        // The camera looks down -Z, so depth is -z.
        const float nearest = -center.z - radius;
        const float farthest = -center.z + radius;
        if (farthest < result.nearDepth) {
            continue;
        }
        std::uint32_t firstX = 0;
        std::uint32_t lastX = ClusterTilesX - 1;
        std::uint32_t firstY = 0;
        std::uint32_t lastY = ClusterTilesY - 1;
        // Spheres that cross the near plane would project past the edges of the view, so they cover all of it.
        if (nearest > result.nearDepth) {
            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            for (int corner = 0; corner < 8; ++corner) {
                const Vector4 point{center.x + ((corner & 1) ? radius : -radius),
                                    center.y + ((corner & 2) ? radius : -radius),
                                    center.z + ((corner & 4) ? radius : -radius), 1.0f};
                const Vector4 clip = projection * point;
                const float x = clip.x / clip.w;
                const float y = clip.y / clip.w;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
            if (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f) {
                continue;
            }
            // Tiles go from the left and, like pixels, from the top, where y is 1.
            const auto tile = [](float ndc, std::uint32_t count) {
                const float position = std::floor((ndc * 0.5f + 0.5f) * static_cast<float>(count));
                return static_cast<std::uint32_t>(std::clamp(position, 0.0f, static_cast<float>(count - 1)));
            };
            firstX = tile(minX, ClusterTilesX);
            lastX = tile(maxX, ClusterTilesX);
            firstY = tile(-maxY, ClusterTilesY);
            lastY = tile(-minY, ClusterTilesY);
        }
        covers.push_back({light.index, firstX, lastX, firstY, lastY,
                          GetClusterSlice(result, std::max(nearest, result.nearDepth)),
                          GetClusterSlice(result, farthest)});
    }

    const auto forEachCluster = [&](const Cover& cover, auto&& visit) {
        for (std::uint32_t slice = cover.firstSlice; slice <= cover.lastSlice; ++slice) {
            for (std::uint32_t y = cover.firstY; y <= cover.lastY; ++y) {
                for (std::uint32_t x = cover.firstX; x <= cover.lastX; ++x) {
                    visit(GetClusterIndex(x, y, slice));
                }
            }
        }
    };
    result.clusters.assign(ClusterCount, {0, 0});
    for (const Cover& cover : covers) {
        forEachCluster(cover, [&](std::uint32_t cluster) { ++result.clusters[cluster][1]; });
    }
    std::uint32_t offset = 0;
    for (auto& [first, count] : result.clusters) {
        first = offset;
        offset += count;
        count = 0;
    }
    result.lightIndices.resize(offset);
    for (const Cover& cover : covers) {
        forEachCluster(cover, [&](std::uint32_t cluster) {
            auto& [first, count] = result.clusters[cluster];
            result.lightIndices[first + count++] = cover.index;
        });
    }
    return result;
}

} // namespace Spot::Detail
