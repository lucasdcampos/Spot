#pragma once

// Backend-internal definitions shared by the NVRHI sources. NVRHI runs the device; the Vulkan instance, device and
// swapchain it needs are made in nvrhi_vulkan.cpp.

#include "graphics/graphics_backend.h"

#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace Spot::Detail {

nvrhi::Format ToNvrhi(TextureFormat format);

struct NvrhiTexture final : Texture::Impl {
    void* GetNativeHandle() const override { return handle.Get(); }

    nvrhi::TextureHandle handle;
};

inline nvrhi::ITexture* GetNvrhiTexture(const Texture& texture) {
    return static_cast<nvrhi::ITexture*>(texture.GetNativeHandle());
}

// The Vulkan objects around an NVRHI device: what NVRHI is given, and the window's swapchain, which it does not make.
class VulkanSurface;

class NvrhiContext final : public RenderContext::Impl {
public:
    int GetWidth() const override { return static_cast<int>(width); }
    int GetHeight() const override { return static_cast<int>(height); }
    std::unique_ptr<RenderPass::Impl> BeginRenderPass(const RenderPassDesc& desc) override;
    bool SetFrameTarget(Texture* target) override;
    bool BlitTextureToFrame(const Texture& source) override;
    void GenerateMipmaps(Texture& texture) override;
    void* GetNativeHandle() const override { return commandList.Get(); }

    nvrhi::IDevice* device = nullptr;
    nvrhi::CommandListHandle commandList;
    nvrhi::ITexture* target = nullptr;
    nvrhi::ITexture* acquiredTarget = nullptr;
    TextureFormat format = TextureFormat::Rgba8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool frameOpen = false;
    bool passOpen = false;
};

class NvrhiDevice final : public GraphicsDevice::Impl {
public:
    // The device may be NVRHI's validation layer over the Vulkan device.
    NvrhiDevice(std::unique_ptr<VulkanSurface> vulkan, nvrhi::vulkan::DeviceHandle vulkanDevice,
                nvrhi::DeviceHandle device);
    ~NvrhiDevice() override;

    std::string_view GetBackendName() const override { return "vulkan (nvrhi)"; }
    std::unique_ptr<Shader> CreateShader(const ShaderDesc& desc) override;
    std::unique_ptr<Pipeline> CreatePipeline(const PipelineDesc& desc) override;
    std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
    void UploadToBuffer(Buffer& buffer, std::span<const std::byte> data, std::uint32_t offset,
                        UploadMode mode) override;
    std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) override;
    std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc) override;
    void UploadToTexture(Texture& texture, std::span<const std::byte> pixels) override;
    std::vector<std::uint8_t> DownloadTexture(const Texture& texture, std::uint32_t mipLevel) override;
    bool SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const override;
    bool SupportsRenderTarget(TextureFormat format) const override;
    TextureFormat GetWindowFormat() const override;
    bool IsVSyncEnabled() const override;
    void SetVSync(bool enabled) override;
    bool BeginFrame() override;
    bool BeginOffscreenFrame(Texture& target) override;
    void EndFrame() override;
    RenderContext::Impl& GetContext() override { return m_context; }
    void* GetNativeHandle() const override { return m_device.Get(); }

private:
    // Records commands into a command list of their own and runs it at once, so they reach the GPU before the frame
    // being recorded, as uploads made during a frame must.
    void RunNow(const std::function<void(nvrhi::ICommandList&)>& record);

    std::unique_ptr<VulkanSurface> m_vulkan;
    nvrhi::vulkan::DeviceHandle m_vulkanDevice;
    nvrhi::DeviceHandle m_device;
    NvrhiContext m_context;
    // A query per frame in flight, so a frame waits for the GPU to finish the one that used its slot before.
    static constexpr std::size_t FramesInFlight = 2;
    std::array<nvrhi::EventQueryHandle, FramesInFlight> m_frameQueries;
    std::array<bool, FramesInFlight> m_frameQueried = {};
    std::size_t m_frameSlot = 0;
    // Whether the open frame draws into the window, which it then presents, rather than into a texture.
    bool m_presenting = false;
};

} // namespace Spot::Detail
