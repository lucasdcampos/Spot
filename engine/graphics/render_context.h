#pragma once

#include "graphics/buffer.h"
#include "graphics/compute.h"
#include "graphics/texture.h"
#include "graphics/texture_table.h"
#include "math/color.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Spot {

class GraphicsDevice;
class Pipeline;
struct GraphicsCapabilities;
class RenderContext;

// A color target a pass draws into besides its first: a render target texture the size of the first, read by the
// fragment shader's SV_Target{1 + index} output.
struct ColorTarget {
    Texture* texture = nullptr;
    std::uint32_t layer = 0;
    std::uint32_t mipLevel = 0;
    std::optional<Color> clearColor = std::nullopt;
    // For a multisampled texture, the single-sample render target its samples are averaged into, as
    // RenderPassDesc::resolveTarget is for the first.
    Texture* resolveTarget = nullptr;
};

struct RenderPassDesc {
    // A texture created as a render target, or null to draw into the frame target (window or offscreen texture).
    Texture* target = nullptr;
    // The face of a cube target to draw into (see TextureType::Cube), and the mip level of the target, which the pass
    // then covers at its size.
    std::uint32_t targetLayer = 0;
    std::uint32_t targetMipLevel = 0;
    // Clears the target first when set; otherwise the pass draws over its current contents.
    std::optional<Color> clearColor = std::nullopt;
    // A depth texture created as a render target, the size of the target, for pipelines that test depth.
    Texture* depthTarget = nullptr;
    // Clears the depth target to this depth first when set, usually 1, the far plane; otherwise keeps its contents.
    std::optional<float> clearDepth = std::nullopt;
    // Multisampled targets only: at the end of the pass their samples are averaged into the resolve target, a
    // single-sample render target of the same size and format, or into the window when resolveToWindow is set. The
    // samples themselves are then discarded.
    Texture* resolveTarget = nullptr;
    bool resolveToWindow = false;
    // Keeps the samples after resolving them, so a later pass can draw more over them and resolve again, as tools
    // drawing over a rendered scene do.
    bool keepSamples = false;
    // Draws only into the depth target, with no color target, as shadow maps are; target, clearColor and the
    // resolve fields must then be unset. Only pipelines created with PipelineDesc::depthOnly draw in such passes.
    bool depthOnly = false;
    // More color targets drawn into at once (multiple render targets), as G-buffers are: up to
    // GraphicsCapabilities::maxColorTargets, the first included. Pipelines drawing in the pass list their formats in
    // PipelineDesc::additionalTargetFormats.
    std::vector<ColorTarget> additionalTargets = {};
};

// The rectangle of the pass's targets that draws are mapped into, in pixels from the top-left corner, and the range
// of depths they are mapped to.
struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

// A rectangle of the pass's targets, in pixels from the top-left corner.
struct ScissorRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

// Commands that draw into a target. Only one pass can be open at a time; it ends when End() is called or when it
// goes out of scope.
class RenderPass {
public:
    ~RenderPass();

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    void End();

    void BindPipeline(const Pipeline& pipeline);
    // Maps later draws of the pass into part of its targets, such as one tile of a texture atlas. Passes start out
    // covering their whole targets. Pixels outside the viewport are not drawn, though they are still cleared.
    void SetViewport(const Viewport& viewport);
    // Clips later draws of the pass to the rectangle, such as the visible part of a scrolled panel, without changing
    // where they are mapped. Passes start out drawing into their whole targets.
    void SetScissor(const ScissorRect& rect);
    void BindVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint32_t offset = 0);
    void BindIndexBuffer(const Buffer& buffer, IndexType type, std::uint32_t offset = 0);
    // Binds the texture and sampler read by the fragment shader's texture at register t{slot}.
    void BindFragmentTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler);
    // Binds a storage buffer read by the stage's StructuredBuffer. Storage buffers come after the stage's textures in
    // its registers: in the vertex shader, slot n is register t{n} of space0 when it samples no textures; in the
    // fragment shader, it is register t{samplers + n} of space2.
    void BindVertexStorageBuffer(std::uint32_t slot, const Buffer& buffer);
    void BindFragmentStorageBuffer(std::uint32_t slot, const Buffer& buffer);
    // Binds the table that pipelines made with PipelineDesc::textureTable read, for later draws of the pass.
    void BindTextureTable(const TextureTable& table);

    // Uniforms set the HLSL constant buffer at register b{slot} of the stage, and stay set for later draws, in this
    // pass and the next ones of the frame. Data follows HLSL constant buffer packing: fields do not cross 16-byte
    // boundaries.
    void PushVertexUniformData(std::uint32_t slot, const void* data, std::uint32_t size);
    void PushFragmentUniformData(std::uint32_t slot, const void* data, std::uint32_t size);

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void PushVertexUniforms(std::uint32_t slot, const T& data) {
        PushVertexUniformData(slot, &data, sizeof(T));
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void PushFragmentUniforms(std::uint32_t slot, const T& data) {
        PushFragmentUniformData(slot, &data, sizeof(T));
    }

    // Draws with the bound pipeline. Without vertex buffers, shaders can build vertices from SV_VertexID.
    void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0,
              std::uint32_t firstInstance = 0);
    // Draws with the bound pipeline and index buffer; vertexOffset is added to every index.
    void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0,
                     std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0);
    // Draws drawCount times, with the DrawIndirectArguments an indirect buffer holds one after the other from the byte
    // offset, which a compute pass may have written: the GPU decides what is drawn. drawCount is at most
    // GraphicsCapabilities::maxIndirectDrawCount.
    void DrawIndirect(const Buffer& buffer, std::uint32_t offset = 0, std::uint32_t drawCount = 1);
    // The same with the bound index buffer and DrawIndexedIndirectArguments.
    void DrawIndexedIndirect(const Buffer& buffer, std::uint32_t offset = 0, std::uint32_t drawCount = 1);

    // The backend's pass object (an SDL_GPURenderPass* with SDL_GPU), for code that needs the backend directly; null
    // once the pass ended, or when it could not begin.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend class RenderContext;

    explicit RenderPass(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
    bool m_pipelineBound = false;
};

// Records the GPU work of one frame. It only exists between GraphicsDevice::BeginFrame() and EndFrame(), which the
// application calls around OnRender().
class RenderContext {
public:
    ~RenderContext();

    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

    // Size of the frame target in pixels, which differs from a window's point size on high-DPI displays.
    int GetWidth() const;
    int GetHeight() const;

    RenderPass BeginRenderPass(const RenderPassDesc& desc = {});
    // Opens a pass that runs compute shaders, where GraphicsCapabilities::compute says so; what it writes is visible
    // to the passes after it.
    ComputePass BeginComputePass(const ComputePassDesc& desc = {});
    // Redirects default passes and window resolves into a same-size/format single-sample target; null restores
    // the acquired frame target. Change it only between passes. Explicit pass targets are unaffected.
    bool SetFrameTarget(Texture* target);
    // Presents a same-size/format single-sample texture into the current frame target, outside a render pass.
    bool BlitTextureToFrame(const Texture& source);
    // Makes every mip level of a texture after the first from the one before it, for render targets drawn at their
    // first level. Not during a render pass.
    void GenerateMipmaps(Texture& texture);

    // Measures how long the GPU takes over what is recorded until the matching EndGpuTimer(), such as a few passes,
    // where GraphicsCapabilities::timestamps says so; GraphicsDevice::GetGpuTimings() reads them once the frame is
    // done. Timers nest, and begin and end between passes.
    void BeginGpuTimer(std::string_view name);
    void EndGpuTimer();

    // The backend's command buffer (an SDL_GPUCommandBuffer* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend class GraphicsDevice;

    RenderContext(Impl& impl, const GraphicsCapabilities& capabilities);

    Impl* m_impl;
    const GraphicsCapabilities* m_capabilities;
};

} // namespace Spot
