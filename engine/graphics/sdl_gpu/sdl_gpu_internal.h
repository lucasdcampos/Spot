#pragma once

// Backend-internal definitions shared by the SDL_GPU sources.

#include "graphics/buffer.h"
#include "graphics/graphics_device.h"
#include "graphics/pipeline.h"
#include "graphics/render_context.h"
#include "graphics/shader.h"
#include "graphics/texture.h"

#include <SDL3/SDL.h>

#include <memory>
#include <optional>

namespace Spot {

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

struct RenderContext::Impl {
    SDL_GPUCommandBuffer* commandBuffer = nullptr;
    SDL_GPUTexture* target = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    bool passOpen = false;
};

struct GraphicsDevice::Impl {
    Impl(std::shared_ptr<SharedGpuDevice> device, SDL_Window* window, std::unique_ptr<RenderContext> context);
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    std::shared_ptr<SharedGpuDevice> device;
    SDL_Window* window;
    bool vsync = true;
    std::optional<bool> pendingVSync;
    bool frameOpen = false;
    std::unique_ptr<RenderContext> context;
};

struct Shader::Impl {
    ~Impl() { SDL_ReleaseGPUShader(device->Get(), handle); }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUShader* handle;
    ShaderStage stage;
};

struct Buffer::Impl {
    ~Impl() { SDL_ReleaseGPUBuffer(device->Get(), handle); }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUBuffer* handle;
    BufferUsage usage;
    std::uint32_t size;
};

struct Texture::Impl {
    ~Impl() { SDL_ReleaseGPUTexture(device->Get(), handle); }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUTexture* handle;
    TextureDesc desc;
    std::uint32_t mipLevels;
};

struct Sampler::Impl {
    ~Impl() { SDL_ReleaseGPUSampler(device->Get(), handle); }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUSampler* handle;
};

SDL_GPUTextureFormat ToSdl(TextureFormat format);

struct Pipeline::Impl {
    ~Impl() { SDL_ReleaseGPUGraphicsPipeline(device->Get(), handle); }

    std::shared_ptr<SharedGpuDevice> device;
    SDL_GPUGraphicsPipeline* handle;
};

} // namespace Spot
