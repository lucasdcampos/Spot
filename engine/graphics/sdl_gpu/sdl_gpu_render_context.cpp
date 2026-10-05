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

void RenderPass::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                      std::uint32_t firstInstance) {
    SPOT_ASSERT(m_native, "The render pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before drawing");
    SDL_DrawGPUPrimitives(static_cast<SDL_GPURenderPass*>(m_native), vertexCount, instanceCount, firstVertex,
                          firstInstance);
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

    SDL_GPUColorTargetInfo target{};
    target.texture = m_impl->target;
    target.store_op = SDL_GPU_STOREOP_STORE;
    if (desc.clearColor) {
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.clear_color = {desc.clearColor->r, desc.clearColor->g, desc.clearColor->b, desc.clearColor->a};
    } else {
        target.load_op = SDL_GPU_LOADOP_LOAD;
    }

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(m_impl->commandBuffer, &target, 1, nullptr);
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
