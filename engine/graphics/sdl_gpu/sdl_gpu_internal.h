#pragma once

// Backend-internal definitions shared by the SDL_GPU sources.

#include "graphics/graphics_backend.h"

#include <SDL3/SDL.h>

#include <memory>
#include <optional>

namespace Spot::Detail {

// Owns the SDL device. The graphics device and every resource share it, so it is only destroyed after the last of
// them, in whatever order the application releases them.
class SharedGpuDevice {
public:
    explicit SharedGpuDevice(SDL_GPUDevice* handle);
    ~SharedGpuDevice();

    SharedGpuDevice(const SharedGpuDevice&) = delete;
    SharedGpuDevice& operator=(const SharedGpuDevice&) = delete;

    SDL_GPUDevice* Get() const { return m_handle; }

private:
    SDL_GPUDevice* m_handle;
};

struct SdlGpuShader final : Shader::Impl {
    ~SdlGpuShader() override { SDL_ReleaseGPUShader(device->Get(), handle); }
    void* GetNativeHandle() const override { return handle; }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUShader* handle = nullptr;
};

struct SdlGpuBuffer final : Buffer::Impl {
    ~SdlGpuBuffer() override { SDL_ReleaseGPUBuffer(device->Get(), handle); }
    void* GetNativeHandle() const override { return handle; }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUBuffer* handle = nullptr;
};

struct SdlGpuTexture final : Texture::Impl {
    ~SdlGpuTexture() override { SDL_ReleaseGPUTexture(device->Get(), handle); }
    void* GetNativeHandle() const override { return handle; }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUTexture* handle = nullptr;
};

struct SdlGpuSampler final : Sampler::Impl {
    ~SdlGpuSampler() override { SDL_ReleaseGPUSampler(device->Get(), handle); }
    void* GetNativeHandle() const override { return handle; }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUSampler* handle = nullptr;
};

struct SdlGpuPipeline final : Pipeline::Impl {
    ~SdlGpuPipeline() override { SDL_ReleaseGPUGraphicsPipeline(device->Get(), handle); }
    void* GetNativeHandle() const override { return handle; }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUGraphicsPipeline* handle = nullptr;
};

inline SDL_GPUTexture* GetSdlTexture(const Texture& texture) {
    return static_cast<SDL_GPUTexture*>(texture.GetNativeHandle());
}

inline SDL_GPUBuffer* GetSdlBuffer(const Buffer& buffer) {
    return static_cast<SDL_GPUBuffer*>(buffer.GetNativeHandle());
}

class SdlGpuContext final : public RenderContext::Impl {
public:
    int GetWidth() const override { return static_cast<int>(width); }
    int GetHeight() const override { return static_cast<int>(height); }
    std::unique_ptr<RenderPass::Impl> BeginRenderPass(const RenderPassDesc& desc) override;
    bool SetFrameTarget(Texture* target) override;
    bool BlitTextureToFrame(const Texture& source) override;
    void GenerateMipmaps(Texture& texture) override;
    void* GetNativeHandle() const override { return commandBuffer; }

    SDL_GPUCommandBuffer* commandBuffer = nullptr;
    SDL_GPUTexture* target = nullptr;
    SDL_GPUTexture* acquiredTarget = nullptr;
    TextureFormat format = TextureFormat::Rgba8;
    Uint32 width = 0;
    Uint32 height = 0;
    bool passOpen = false;
};

class SdlGpuDevice final : public GraphicsDevice::Impl {
public:
    SdlGpuDevice(std::shared_ptr<SharedGpuDevice> device, SDL_Window* window);
    ~SdlGpuDevice() override;

    std::string_view GetBackendName() const override;
    const GraphicsCapabilities& GetCapabilities() const override { return capabilities; }
    std::unique_ptr<Shader> CreateShader(const ShaderDesc& desc) override;
    std::unique_ptr<Pipeline> CreatePipeline(const PipelineDesc& desc) override;
    std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
    void UploadToBuffer(Buffer& buffer, std::span<const std::byte> data, std::uint32_t offset,
                        UploadMode mode) override;
    std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) override;
    std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc) override;
    void UploadToTexture(Texture& texture, std::uint32_t mipLevel, std::span<const std::byte> pixels,
                         bool generateMipmaps) override;
    std::vector<std::uint8_t> DownloadTexture(const Texture& texture, std::uint32_t mipLevel) override;
    bool SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const override;
    bool SupportsRenderTarget(TextureFormat format) const override;
    bool SupportsSampling(TextureFormat format) const override;
    TextureFormat GetWindowFormat() const override;
    bool IsVSyncEnabled() const override;
    void SetVSync(bool enabled) override;
    bool BeginFrame() override;
    bool BeginOffscreenFrame(Texture& target) override;
    void EndFrame() override;
    RenderContext::Impl& GetContext() override { return context; }
    void* GetNativeHandle() const override { return shared->Get(); }

    std::shared_ptr<SharedGpuDevice> shared;
    SDL_Window* window;
    GraphicsCapabilities capabilities;
    bool vsync = true;
    std::optional<bool> pendingVSync;
    SDL_GPUSwapchainComposition composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    bool frameOpen = false;
    SdlGpuContext context;
};

SDL_GPUTextureFormat ToSdl(TextureFormat format);
// Nothing for counts SDL_GPU has no value for.
std::optional<SDL_GPUSampleCount> ToSdlSampleCount(std::uint32_t sampleCount);

} // namespace Spot::Detail
