#include "graphics/render_context.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/sdl_gpu/sdl_gpu_internal.h"

namespace Spot {

RenderPass::RenderPass(RenderContext& context, void* native) : m_context(&context), m_native(native) {
}

RenderPass::~RenderPass() {
    End();
}

void RenderPass::End() {
    if (!m_native) {
        return;
    }

    SDL_EndGPURenderPass(static_cast<SDL_GPURenderPass*>(m_native));
    m_native = nullptr;
    m_context->m_impl->passOpen = false;
}

void RenderPass::BindPipeline(const Pipeline& pipeline) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    SDL_BindGPUGraphicsPipeline(static_cast<SDL_GPURenderPass*>(m_native),
                                static_cast<SDL_GPUGraphicsPipeline*>(pipeline.GetNativeHandle()));
    m_pipelineBound = true;
}

void RenderPass::SetViewport(const Viewport& viewport) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    const SDL_GPUViewport native{viewport.x,      viewport.y,        viewport.width,
                                 viewport.height, viewport.minDepth, viewport.maxDepth};
    SDL_SetGPUViewport(static_cast<SDL_GPURenderPass*>(m_native), &native);
}

void RenderPass::SetScissor(const ScissorRect& rect) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    SPOT_ASSERT(rect.width >= 0 && rect.height >= 0, "Scissor rectangles cannot have a negative size");
    const SDL_Rect native{rect.x, rect.y, rect.width, rect.height};
    SDL_SetGPUScissor(static_cast<SDL_GPURenderPass*>(m_native), &native);
}

void RenderPass::BindVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint32_t offset) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Vertex, "Only vertex buffers can be bound as vertex buffers");
    SDL_GPUBufferBinding binding{};
    binding.buffer = static_cast<SDL_GPUBuffer*>(buffer.GetNativeHandle());
    binding.offset = offset;
    SDL_BindGPUVertexBuffers(static_cast<SDL_GPURenderPass*>(m_native), slot, &binding, 1);
}

void RenderPass::BindIndexBuffer(const Buffer& buffer, IndexType type, std::uint32_t offset) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Index, "Only index buffers can be bound as index buffers");
    SDL_GPUBufferBinding binding{};
    binding.buffer = static_cast<SDL_GPUBuffer*>(buffer.GetNativeHandle());
    binding.offset = offset;
    SDL_BindGPUIndexBuffer(static_cast<SDL_GPURenderPass*>(m_native), &binding,
                           type == IndexType::UInt16 ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

void RenderPass::BindFragmentTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    SDL_GPUTextureSamplerBinding binding{};
    binding.texture = static_cast<SDL_GPUTexture*>(texture.GetNativeHandle());
    binding.sampler = static_cast<SDL_GPUSampler*>(sampler.GetNativeHandle());
    SDL_BindGPUFragmentSamplers(static_cast<SDL_GPURenderPass*>(m_native), slot, &binding, 1);
}

void RenderPass::PushVertexUniformData(std::uint32_t slot, const void* data, std::uint32_t size) {
    SDL_PushGPUVertexUniformData(m_context->m_impl->commandBuffer, slot, data, size);
}

void RenderPass::PushFragmentUniformData(std::uint32_t slot, const void* data, std::uint32_t size) {
    SDL_PushGPUFragmentUniformData(m_context->m_impl->commandBuffer, slot, data, size);
}

void RenderPass::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                      std::uint32_t firstInstance) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before drawing");
    SDL_DrawGPUPrimitives(static_cast<SDL_GPURenderPass*>(m_native), vertexCount, instanceCount, firstVertex,
                          firstInstance);
}

void RenderPass::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                             std::int32_t vertexOffset, std::uint32_t firstInstance) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before drawing");
    SDL_DrawGPUIndexedPrimitives(static_cast<SDL_GPURenderPass*>(m_native), indexCount, instanceCount, firstIndex,
                                 vertexOffset, firstInstance);
}

RenderContext::RenderContext() : m_impl(std::make_unique<Impl>()) {
}

RenderContext::~RenderContext() = default;

int RenderContext::GetWidth() const {
    return static_cast<int>(m_impl->width);
}

int RenderContext::GetHeight() const {
    return static_cast<int>(m_impl->height);
}

RenderPass RenderContext::BeginRenderPass(const RenderPassDesc& desc) {
    SPOT_ASSERT(!m_impl->passOpen, "Only one render pass can be open at a time");

    SPOT_ASSERT(!desc.depthOnly || (desc.depthTarget && !desc.target && !desc.clearColor && !desc.resolveTarget &&
                                    !desc.resolveToWindow),
                "Depth-only passes need a depth target and nothing to draw colors into");
    SPOT_ASSERT(!desc.target || desc.target->IsRenderTarget(), "Render pass targets must be created as render targets");
    SPOT_ASSERT(!desc.target || !IsDepthFormat(desc.target->GetFormat()),
                "Depth textures go in RenderPassDesc::depthTarget");

    SDL_GPUColorTargetInfo target{};
    target.texture = desc.target ? static_cast<SDL_GPUTexture*>(desc.target->GetNativeHandle()) : m_impl->target;
    target.store_op = SDL_GPU_STOREOP_STORE;
    if (desc.clearColor) {
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.clear_color = {desc.clearColor->r, desc.clearColor->g, desc.clearColor->b, desc.clearColor->a};
    } else {
        target.load_op = SDL_GPU_LOADOP_LOAD;
    }

    const bool resolves = desc.resolveTarget || desc.resolveToWindow;
    SPOT_ASSERT(!(desc.resolveTarget && desc.resolveToWindow), "A pass resolves into one texture or the window");
    SPOT_ASSERT(resolves == (desc.target && desc.target->GetSampleCount() > 1),
                "Multisampled targets must be resolved, and only they can be");
    if (desc.resolveTarget) {
        SPOT_ASSERT(desc.resolveTarget->IsRenderTarget() && desc.resolveTarget->GetSampleCount() == 1,
                    "Resolve targets must be single-sample render targets");
        SPOT_ASSERT(desc.resolveTarget->GetFormat() == desc.target->GetFormat() &&
                        desc.resolveTarget->GetWidth() == desc.target->GetWidth() &&
                        desc.resolveTarget->GetHeight() == desc.target->GetHeight(),
                    "Resolve targets must have the size and format of the target");
        target.resolve_texture = static_cast<SDL_GPUTexture*>(desc.resolveTarget->GetNativeHandle());
    } else if (desc.resolveToWindow) {
        SPOT_ASSERT(desc.target->GetWidth() == m_impl->width && desc.target->GetHeight() == m_impl->height,
                    "Targets resolved into the window must be the size of its frame");
        target.resolve_texture = m_impl->target;
    }
    if (resolves) {
        target.store_op = desc.keepSamples ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
    }

    SDL_GPUDepthStencilTargetInfo depth{};
    if (desc.depthTarget) {
        SPOT_ASSERT(desc.depthTarget->IsRenderTarget() && IsDepthFormat(desc.depthTarget->GetFormat()),
                    "Depth targets must be depth textures created as render targets");
        SPOT_ASSERT(desc.depthOnly ||
                        (desc.depthTarget->GetWidth() == (desc.target ? desc.target->GetWidth() : m_impl->width) &&
                         desc.depthTarget->GetHeight() == (desc.target ? desc.target->GetHeight() : m_impl->height)),
                    "The depth target must be the size of the target");
        depth.texture = static_cast<SDL_GPUTexture*>(desc.depthTarget->GetNativeHandle());
        depth.load_op = desc.clearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depth.clear_depth = desc.clearDepth.value_or(1.0f);
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    }

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(m_impl->commandBuffer, desc.depthOnly ? nullptr : &target,
                                                     desc.depthOnly ? 0 : 1, desc.depthTarget ? &depth : nullptr);
    if (!pass) {
        Log::GetEngineLogger().Error("Failed to begin render pass: {}", SDL_GetError());
    }
    m_impl->passOpen = pass != nullptr;
    return RenderPass(*this, pass);
}

void* RenderContext::GetNativeHandle() const {
    return m_impl->commandBuffer;
}

} // namespace Spot
