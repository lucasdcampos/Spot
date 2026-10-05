#pragma once

#include "graphics/buffer.h"
#include "graphics/color.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>

namespace Spot {

class GraphicsDevice;
class Pipeline;
class RenderContext;

struct RenderPassDesc {
    // Clears the target first when set; otherwise the pass draws over its current contents.
    std::optional<Color> clearColor = std::nullopt;
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
    void BindVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint32_t offset = 0);
    void BindIndexBuffer(const Buffer& buffer, IndexType type, std::uint32_t offset = 0);

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

    // Size of the frame's target in pixels, which differs from the window size on high-DPI displays.
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
