#pragma once

// Engine-internal: how Renderer3D batches the meshes of a frame and draws them, into the view and the camera's depth.

#include "spot/render/renderer_3d_internal.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Spot::Detail {

class MeshBatcher {
public:
    explicit MeshBatcher(Renderer3DState& state);

    bool IsValid() const;

    // Queues the level of detail of the object, drawn with the material.
    void AddCommand(const RenderMeshObject& object, const Material& material, std::uint32_t level = 0);
    // Leaves what the camera cannot see out of the view's pass, gives the commands their looks and sorts them, so
    // those that look the same are drawn as instances of one draw.
    void Prepare();
    // Sends every command's transforms, and the skinned meshes' joints, to their buffers, in the commands' order.
    bool UploadInstances();
    // Draws the commands in view into the pass, which has the frame's lighting bound.
    void Flush(RenderPass& pass);
    // Draws what the view's pass drew, but transparent surfaces, into readable single-sample depth. Repeated meshes
    // share instanced draws; GPU worlds reuse their compact visible lists when available.
    const Texture* RenderCameraDepth(RenderContext& context);
    // What RenderCameraDepth() drew last.
    const Texture* GetCameraDepth() const { return m_cameraDepth.get(); }

private:
    // Gives the commands their looks.
    void AssignLooks();
    const Pipeline* GetPipeline(const DrawCommand& command) const;

    Renderer3DState& m_state;
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Pipeline> m_doubleSidedPipeline;
    std::unique_ptr<Pipeline> m_transparentPipeline;
    std::unique_ptr<Pipeline> m_doubleSidedTransparentPipeline;
    // Single-sided ones for mirroring transforms, which cull front faces, as the mirror winds them clockwise.
    std::unique_ptr<Pipeline> m_mirroredPipeline;
    std::unique_ptr<Pipeline> m_mirroredTransparentPipeline;
    std::array<std::unique_ptr<Pipeline>, 6> m_skinnedPipelines;
    // Bound in place of the instances a culling pass kept, for draws without one.
    std::unique_ptr<Buffer> m_noVisible;

    std::array<std::unique_ptr<Pipeline>, 12> m_cameraDepthPipelines;
    std::unique_ptr<Texture> m_cameraDepth;
    std::unique_ptr<Buffer> m_cameraDepthIndices;
    std::vector<std::uint32_t> m_cameraDepthOrder;

    // What Prepare() sorts the commands by, small enough to move around cheaply, and where they go once sorted.
    struct SortEntry {
        bool transparent;
        bool hidden;
        bool doubleSided;
        bool mirrored;
        AlphaMode alphaMode;
        std::uint32_t look;
        const Mesh* mesh;
        std::uint32_t firstIndex;
        // Along the camera's forward axis for surfaces that hide what is behind; the squared distance from the eye
        // for transparent ones.
        float depth;
        std::uint32_t index;
    };
    std::vector<SortEntry> m_sortEntries;
    std::vector<DrawCommand> m_sortedCommands;
};

} // namespace Spot::Detail
