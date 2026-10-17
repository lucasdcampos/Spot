#include "core/assert.h"
#include "core/log.h"
#include "graphics/sdl_gpu/sdl_gpu_internal.h"

#include <algorithm>
#include <cstdint>

namespace Spot::Detail {

namespace {

class SdlGpuPass final : public RenderPass::Impl {
public:
    SdlGpuPass(SdlGpuContext& context, SDL_GPURenderPass* pass) : m_context(context), m_pass(pass) {}

    void End() override {
        SDL_EndGPURenderPass(m_pass);
        m_context.passOpen = false;
    }

    void BindPipeline(const Pipeline& pipeline) override {
        SDL_BindGPUGraphicsPipeline(m_pass, static_cast<SDL_GPUGraphicsPipeline*>(pipeline.GetNativeHandle()));
    }

    void SetViewport(const Viewport& viewport) override {
        const SDL_GPUViewport native{viewport.x,      viewport.y,        viewport.width,
                                     viewport.height, viewport.minDepth, viewport.maxDepth};
        SDL_SetGPUViewport(m_pass, &native);
    }

    void SetScissor(const ScissorRect& rect) override {
        const SDL_Rect native{rect.x, rect.y, rect.width, rect.height};
        SDL_SetGPUScissor(m_pass, &native);
    }

    void BindVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint32_t offset) override {
        SDL_GPUBufferBinding binding{};
        binding.buffer = GetSdlBuffer(buffer);
        binding.offset = offset;
        SDL_BindGPUVertexBuffers(m_pass, slot, &binding, 1);
    }

    void BindIndexBuffer(const Buffer& buffer, IndexType type, std::uint32_t offset) override {
        SDL_GPUBufferBinding binding{};
        binding.buffer = GetSdlBuffer(buffer);
        binding.offset = offset;
        SDL_BindGPUIndexBuffer(m_pass, &binding,
                               type == IndexType::UInt16 ? SDL_GPU_INDEXELEMENTSIZE_16BIT
                                                         : SDL_GPU_INDEXELEMENTSIZE_32BIT);
    }

    void BindFragmentTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) override {
        SDL_GPUTextureSamplerBinding binding{};
        binding.texture = GetSdlTexture(texture);
        binding.sampler = static_cast<SDL_GPUSampler*>(sampler.GetNativeHandle());
        SDL_BindGPUFragmentSamplers(m_pass, slot, &binding, 1);
    }

    void BindVertexStorageBuffer(std::uint32_t slot, const Buffer& buffer) override {
        auto* handle = GetSdlBuffer(buffer);
        SDL_BindGPUVertexStorageBuffers(m_pass, slot, &handle, 1);
    }

    void BindFragmentStorageBuffer(std::uint32_t slot, const Buffer& buffer) override {
        auto* handle = GetSdlBuffer(buffer);
        SDL_BindGPUFragmentStorageBuffers(m_pass, slot, &handle, 1);
    }

    void PushVertexUniformData(std::uint32_t slot, const void* data, std::uint32_t size) override {
        SDL_PushGPUVertexUniformData(m_context.commandBuffer, slot, data, size);
    }

    void PushFragmentUniformData(std::uint32_t slot, const void* data, std::uint32_t size) override {
        SDL_PushGPUFragmentUniformData(m_context.commandBuffer, slot, data, size);
    }

    void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
              std::uint32_t firstInstance) override {
        SDL_DrawGPUPrimitives(m_pass, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                     std::int32_t vertexOffset, std::uint32_t firstInstance) override {
        SDL_DrawGPUIndexedPrimitives(m_pass, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void* GetNativeHandle() const override { return m_pass; }

private:
    SdlGpuContext& m_context;
    SDL_GPURenderPass* m_pass;
};

} // namespace

std::unique_ptr<RenderPass::Impl> SdlGpuContext::BeginRenderPass(const RenderPassDesc& desc) {
    SPOT_ASSERT(!passOpen, "Only one render pass can be open at a time");

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = desc.target ? GetSdlTexture(*desc.target) : target;
    colorTarget.layer_or_depth_plane = desc.targetLayer;
    colorTarget.mip_level = desc.targetMipLevel;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    if (desc.clearColor) {
        colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        colorTarget.clear_color = {desc.clearColor->r, desc.clearColor->g, desc.clearColor->b, desc.clearColor->a};
    } else {
        colorTarget.load_op = SDL_GPU_LOADOP_LOAD;
    }

    if (desc.resolveTarget) {
        colorTarget.resolve_texture = GetSdlTexture(*desc.resolveTarget);
    } else if (desc.resolveToWindow) {
        SPOT_ASSERT(desc.target->GetWidth() == width && desc.target->GetHeight() == height,
                    "Targets resolved into the window must be the size of its frame");
        colorTarget.resolve_texture = target;
    }
    if (desc.resolveTarget || desc.resolveToWindow) {
        colorTarget.store_op = desc.keepSamples ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
    }

    SDL_GPUDepthStencilTargetInfo depth{};
    if (desc.depthTarget) {
        SPOT_ASSERT(desc.depthOnly || (desc.depthTarget->GetWidth() ==
                                           (desc.target ? std::max(desc.target->GetWidth() >> desc.targetMipLevel, 1u)
                                                        : static_cast<std::uint32_t>(width)) &&
                                       desc.depthTarget->GetHeight() ==
                                           (desc.target ? std::max(desc.target->GetHeight() >> desc.targetMipLevel, 1u)
                                                        : static_cast<std::uint32_t>(height))),
                    "The depth target must be the size of the target");
        depth.texture = GetSdlTexture(*desc.depthTarget);
        depth.load_op = desc.clearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depth.clear_depth = desc.clearDepth.value_or(1.0f);
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    }

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, desc.depthOnly ? nullptr : &colorTarget,
                                                     desc.depthOnly ? 0 : 1, desc.depthTarget ? &depth : nullptr);
    if (!pass) {
        Log::GetEngineLogger().Error("Failed to begin render pass: {}", SDL_GetError());
        return nullptr;
    }
    passOpen = true;
    return std::make_unique<SdlGpuPass>(*this, pass);
}

void SdlGpuContext::GenerateMipmaps(Texture& texture) {
    SPOT_ASSERT(!passOpen, "Mipmaps cannot be generated during a render pass");
    if (texture.GetMipLevelCount() > 1) {
        SDL_GenerateMipmapsForGPUTexture(commandBuffer, GetSdlTexture(texture));
    }
}

bool SdlGpuContext::SetFrameTarget(Texture* frameTarget) {
    if (passOpen || !commandBuffer || !acquiredTarget) {
        return false;
    }
    if (frameTarget && (!frameTarget->IsRenderTarget() || frameTarget->GetSampleCount() != 1 ||
                        frameTarget->GetLayerCount() != 1 || frameTarget->GetFormat() != format ||
                        frameTarget->GetWidth() != width || frameTarget->GetHeight() != height)) {
        Log::GetEngineLogger().Warning("Frame targets must match the acquired frame's size and format");
        return false;
    }
    target = frameTarget ? GetSdlTexture(*frameTarget) : acquiredTarget;
    return true;
}

bool SdlGpuContext::BlitTextureToFrame(const Texture& source) {
    auto* texture = GetSdlTexture(source);
    if (passOpen || !commandBuffer || !target || texture == target || source.GetSampleCount() != 1 ||
        source.GetLayerCount() != 1 || source.GetFormat() != format || source.GetWidth() != width ||
        source.GetHeight() != height) {
        return false;
    }
    SDL_GPUBlitInfo blit{};
    blit.source.texture = texture;
    blit.source.w = width;
    blit.source.h = height;
    blit.destination.texture = target;
    blit.destination.w = width;
    blit.destination.h = height;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(commandBuffer, &blit);
    return true;
}

} // namespace Spot::Detail
