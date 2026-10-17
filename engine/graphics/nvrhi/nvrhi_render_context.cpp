#include "graphics/nvrhi/nvrhi_internal.h"

#include "core/assert.h"
#include "core/log.h"

#include <utility>

namespace Spot::Detail {

namespace {

void WarnDrawUnsupported() {
    static bool warned = false;
    if (!std::exchange(warned, true)) {
        Log::GetEngineLogger().Error("The Vulkan backend cannot draw yet");
    }
}

// NVRHI has no render pass objects to begin: it records draws against a framebuffer, beginning and ending Vulkan's
// passes itself. A pass here clears its targets when it begins and resolves them when it ends.
class NvrhiPass final : public RenderPass::Impl {
public:
    NvrhiPass(NvrhiContext& context, const RenderPassDesc& desc) : m_context(context), m_desc(desc) {}

    void End() override {
        nvrhi::ICommandList& commandList = *m_context.commandList;
        if (m_desc.resolveTarget || m_desc.resolveToWindow) {
            nvrhi::ITexture* destination =
                m_desc.resolveTarget ? GetNvrhiTexture(*m_desc.resolveTarget) : m_context.target;
            commandList.resolveTexture(destination, nvrhi::AllSubresources, GetNvrhiTexture(*m_desc.target),
                                       nvrhi::AllSubresources);
        }
        m_context.passOpen = false;
    }

    void BindPipeline(const Pipeline&) override { WarnDrawUnsupported(); }
    void SetViewport(const Viewport&) override {}
    void SetScissor(const ScissorRect&) override {}
    void BindVertexBuffer(std::uint32_t, const Buffer&, std::uint32_t) override {}
    void BindIndexBuffer(const Buffer&, IndexType, std::uint32_t) override {}
    void BindFragmentTexture(std::uint32_t, const Texture&, const Sampler&) override {}
    void BindVertexStorageBuffer(std::uint32_t, const Buffer&) override {}
    void BindFragmentStorageBuffer(std::uint32_t, const Buffer&) override {}
    void PushVertexUniformData(std::uint32_t, const void*, std::uint32_t) override {}
    void PushFragmentUniformData(std::uint32_t, const void*, std::uint32_t) override {}
    void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { WarnDrawUnsupported(); }
    void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {
        WarnDrawUnsupported();
    }
    void* GetNativeHandle() const override { return m_context.commandList.Get(); }

private:
    NvrhiContext& m_context;
    RenderPassDesc m_desc;
};

} // namespace

std::unique_ptr<RenderPass::Impl> NvrhiContext::BeginRenderPass(const RenderPassDesc& desc) {
    SPOT_ASSERT(!passOpen, "Only one render pass can be open at a time");
    SPOT_ASSERT(frameOpen, "Render passes need an open frame");
    if (!desc.depthOnly && desc.clearColor) {
        nvrhi::ITexture* color = desc.target ? GetNvrhiTexture(*desc.target) : target;
        const Color& clear = *desc.clearColor;
        commandList->clearTextureFloat(color, nvrhi::TextureSubresourceSet(desc.targetMipLevel, 1, desc.targetLayer, 1),
                                       nvrhi::Color(clear.r, clear.g, clear.b, clear.a));
    }
    if (desc.depthTarget && desc.clearDepth) {
        commandList->clearDepthStencilTexture(GetNvrhiTexture(*desc.depthTarget), nvrhi::AllSubresources, true,
                                              *desc.clearDepth, false, 0);
    }
    passOpen = true;
    return std::make_unique<NvrhiPass>(*this, desc);
}

bool NvrhiContext::SetFrameTarget(Texture* frameTarget) {
    if (passOpen || !frameOpen || !acquiredTarget) {
        return false;
    }
    if (frameTarget && (!frameTarget->IsRenderTarget() || frameTarget->GetSampleCount() != 1 ||
                        frameTarget->GetLayerCount() != 1 || frameTarget->GetFormat() != format ||
                        frameTarget->GetWidth() != width || frameTarget->GetHeight() != height)) {
        Log::GetEngineLogger().Warning("Frame targets must match the acquired frame's size and format");
        return false;
    }
    target = frameTarget ? GetNvrhiTexture(*frameTarget) : acquiredTarget;
    return true;
}

bool NvrhiContext::BlitTextureToFrame(const Texture& source) {
    nvrhi::ITexture* texture = GetNvrhiTexture(source);
    if (passOpen || !frameOpen || !target || texture == target || source.GetSampleCount() != 1 ||
        source.GetLayerCount() != 1 || source.GetFormat() != format || source.GetWidth() != width ||
        source.GetHeight() != height) {
        return false;
    }
    commandList->copyTexture(target, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
    return true;
}

void NvrhiContext::GenerateMipmaps(Texture& texture) {
    SPOT_ASSERT(!passOpen, "Mipmaps cannot be generated during a render pass");
    if (texture.GetMipLevelCount() > 1) {
        static bool warned = false;
        if (!std::exchange(warned, true)) {
            Log::GetEngineLogger().Error("The Vulkan backend cannot generate mipmaps yet");
        }
    }
}

} // namespace Spot::Detail
