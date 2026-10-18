#include "core/assert.h"
#include "core/log.h"
#include "graphics/sdl_gpu/sdl_gpu_internal.h"

#include <algorithm>
#include <cstdint>
#include <vector>

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

    void DrawIndirect(const Buffer& buffer, std::uint32_t offset, std::uint32_t drawCount) override {
        SDL_DrawGPUPrimitivesIndirect(m_pass, GetSdlBuffer(buffer), offset, drawCount);
    }

    void DrawIndexedIndirect(const Buffer& buffer, std::uint32_t offset, std::uint32_t drawCount) override {
        SDL_DrawGPUIndexedPrimitivesIndirect(m_pass, GetSdlBuffer(buffer), offset, drawCount);
    }

    void* GetNativeHandle() const override { return m_pass; }

private:
    SdlGpuContext& m_context;
    SDL_GPURenderPass* m_pass;
};

class SdlGpuComputePass final : public ComputePass::Impl {
public:
    SdlGpuComputePass(SdlGpuContext& context, SDL_GPUComputePass* pass) : m_context(context), m_pass(pass) {}

    void End() override {
        SDL_EndGPUComputePass(m_pass);
        m_context.passOpen = false;
    }

    void BindPipeline(const ComputePipeline& pipeline) override {
        SDL_BindGPUComputePipeline(m_pass, static_cast<SDL_GPUComputePipeline*>(pipeline.GetNativeHandle()));
    }

    void BindTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) override {
        SDL_GPUTextureSamplerBinding binding{};
        binding.texture = GetSdlTexture(texture);
        binding.sampler = static_cast<SDL_GPUSampler*>(sampler.GetNativeHandle());
        SDL_BindGPUComputeSamplers(m_pass, slot, &binding, 1);
    }

    void BindStorageBuffer(std::uint32_t slot, const Buffer& buffer) override {
        auto* handle = GetSdlBuffer(buffer);
        SDL_BindGPUComputeStorageBuffers(m_pass, slot, &handle, 1);
    }

    void PushUniformData(std::uint32_t slot, const void* data, std::uint32_t size) override {
        SDL_PushGPUComputeUniformData(m_context.commandBuffer, slot, data, size);
    }

    void Dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) override {
        SDL_DispatchGPUCompute(m_pass, groupsX, groupsY, groupsZ);
    }

    void DispatchIndirect(const Buffer& buffer, std::uint32_t offset) override {
        SDL_DispatchGPUComputeIndirect(m_pass, GetSdlBuffer(buffer), offset);
    }

    void* GetNativeHandle() const override { return m_pass; }

private:
    SdlGpuContext& m_context;
    SDL_GPUComputePass* m_pass;
};

} // namespace

std::unique_ptr<ComputePass::Impl> SdlGpuContext::BeginComputePass(const ComputePassDesc& desc) {
    SPOT_ASSERT(!passOpen, "Only one pass can be open at a time");
    std::vector<SDL_GPUStorageTextureReadWriteBinding> textures(desc.textures.size());
    for (std::size_t i = 0; i < desc.textures.size(); ++i) {
        textures[i].texture = GetSdlTexture(*desc.textures[i].texture);
        textures[i].mip_level = desc.textures[i].mipLevel;
    }
    std::vector<SDL_GPUStorageBufferReadWriteBinding> buffers(desc.buffers.size());
    for (std::size_t i = 0; i < desc.buffers.size(); ++i) {
        buffers[i].buffer = GetSdlBuffer(*desc.buffers[i]);
    }
    SDL_GPUComputePass* pass =
        SDL_BeginGPUComputePass(commandBuffer, textures.data(), static_cast<Uint32>(textures.size()), buffers.data(),
                                static_cast<Uint32>(buffers.size()));
    if (!pass) {
        Log::GetEngineLogger().Error("Failed to begin compute pass: {}", SDL_GetError());
        return nullptr;
    }
    passOpen = true;
    return std::make_unique<SdlGpuComputePass>(*this, pass);
}

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

    // The pass's other color targets, after the first.
    SDL_GPUColorTargetInfo colorTargets[4] = {colorTarget};
    for (std::size_t i = 0; i < desc.additionalTargets.size(); ++i) {
        const ColorTarget& color = desc.additionalTargets[i];
        SDL_GPUColorTargetInfo& info = colorTargets[i + 1];
        info.texture = GetSdlTexture(*color.texture);
        info.layer_or_depth_plane = color.layer;
        info.mip_level = color.mipLevel;
        info.load_op = color.clearColor ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        if (color.clearColor) {
            info.clear_color = {color.clearColor->r, color.clearColor->g, color.clearColor->b, color.clearColor->a};
        }
        info.store_op = SDL_GPU_STOREOP_STORE;
        if (color.resolveTarget) {
            info.resolve_texture = GetSdlTexture(*color.resolveTarget);
            info.store_op = desc.keepSamples ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
        }
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

    const auto colorCount = static_cast<Uint32>(desc.depthOnly ? 0 : 1 + desc.additionalTargets.size());
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, desc.depthOnly ? nullptr : colorTargets, colorCount,
                                                     desc.depthTarget ? &depth : nullptr);
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
