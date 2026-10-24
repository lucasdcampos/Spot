#pragma once

// Engine-internal: the render worlds Renderer3D keeps on the GPU, and the culling of their instances there.

#include "spot/render/fullscreen_pass.h"
#include "spot/render/renderer_3d_internal.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Spot::Detail {

// The most instances of a batch of a render world kept on the GPU: views leave out batches outside them whole, and
// cull the instances of the others in the shaders, so smaller batches leave the GPU less to cull, larger ones the CPU
// fewer draws.
inline constexpr std::uint32_t WorldBatchSize = 128;

// A run of a world's instances drawn the same way.
struct WorldBatch {
    const Mesh* mesh;
    const Material* material;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    bool mirrored;
    bool castsShadows;
    std::uint32_t layer;
    // Of its instances, in the world's buffer.
    std::uint32_t first;
    std::uint32_t count;
    BoundingSphere bounds;
    bool boundsChanged;
};

// A render world as kept on the GPU: its objects' instances, in batches of those drawn the same way, so drawing it
// costs a command per batch, and only what changed is uploaded again.
struct WorldCache {
    std::uint64_t worldId = 0;
    std::uint64_t structureVersion = 0;
    std::uint64_t changeCount = 0;
    bool built = false;
    std::unique_ptr<Buffer> instances;
    std::vector<InstanceData> instanceData;
    // The world's object and level of detail at each instance, and the instances of each object: those in slots from
    // slotStarts[object] to slotStarts[object + 1].
    std::vector<std::uint32_t> objects;
    std::vector<std::uint32_t> levels;
    std::vector<std::uint32_t> slotStarts;
    std::vector<std::uint32_t> slots;
    std::vector<std::uint32_t> batchOfInstance;
    std::vector<WorldBatch> batches;
    std::uint64_t lastDrawn = 0;
    // For the culling pass: each batch's first instance, count, index count and first index, the instances it keeps,
    // and the batches' indirect draws.
    std::unique_ptr<Buffer> batchTable;
    std::unique_ptr<Buffer> visible;
    std::unique_ptr<Buffer> arguments;
};

class WorldCuller {
public:
    explicit WorldCuller(Renderer3DState& state);

    // Brings the world's instances on the GPU up to date. Null when its buffer cannot be made, after logging why; the
    // world's objects are then drawn one by one.
    WorldCache* Update(const RenderWorld& world);
    // Culls the instances of the frame's worlds on the GPU, against the view, their levels of detail and what the last
    // frame of the view drew, into lists the view's pass draws indirectly. Does nothing without GPU culling.
    void Cull();
    // Keeps the farthest depth of each tile of what the view's pass drew, for the next frame to cull against.
    void UpdateOcclusionTiles();
    // Lets go of worlds not drawn for a while, with their buffers.
    void ReleaseUnused();

private:
    Renderer3DState& m_state;
    std::vector<std::unique_ptr<WorldCache>> m_caches;
    // Occlusion culling: the last frame's depth, by tiles, and the view it was drawn from, which only the next frame
    // of the same view culls against.
    std::unique_ptr<FullscreenPass> m_tilePass;
    std::unique_ptr<ComputePipeline> m_pipeline;
    std::unique_ptr<Sampler> m_sampler;
    std::unique_ptr<Texture> m_tiles;
    // Stands in for tiles when there are none, hiding nothing.
    std::unique_ptr<Texture> m_noTiles;
    Matrix4 m_viewProjection;
    const Texture* m_depth = nullptr;
    bool m_valid = false;
};

} // namespace Spot::Detail
