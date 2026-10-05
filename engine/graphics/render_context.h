#pragma once

#include "graphics/buffer.h"
#include "graphics/color.h"
#include "graphics/texture.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>

namespace Spot {

class GraphicsDevice;
class Pipeline;
class RenderContext;

struct RenderPassDesc {
    // A texture created as a render target, or null to draw into the window.
    Texture* target = nullptr;
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
    // Draws only into the depth target, with no color target, as shadow maps are; target, clearColor and the
    // resolve fields must then be unset. Only pipelines created with PipelineDesc::depthOnly draw in such passes.
    bool depthOnly = false;
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
    void BindVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint32_t offset = 0);
    void BindIndexBuffer(const Buffer& buffer, IndexType type, std::uint32_t offset = 0);
    // Binds the texture and sampler read by the fragment shader's texture at register t{slot}.
    void BindFragmentTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler);

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

    // The backend's pass object (an SDL_GPURenderPass* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const { return m_native; }

private:
    friend class RenderContext;

    RenderPass(RenderContext& context, void* native);

    RenderContext* m_context;
    void* m_native;
    bool m_pipelineBound = false;
};

// Records the GPU work of one frame. It only exists between GraphicsDevice::BeginFrame() and EndFrame(), which the
// application calls around OnRender().
class RenderContext {
public:
    ~RenderContext();

    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

    // Size of the window's frame in pixels, which differs from the window size on high-DPI displays.
    int GetWidth() const;
    int GetHeight() const;

    RenderPass BeginRenderPass(const RenderPassDesc& desc = {});

    // The backend's command buffer (an SDL_GPUCommandBuffer* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

private:
    friend class GraphicsDevice;
    friend class RenderPass;

    struct Impl;

    RenderContext();

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
