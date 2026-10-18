#include "core/assert.h"
#include "core/log.h"
#include "graphics/sdl_gpu/sdl_gpu_internal.h"
#include "platform/platform.h"
#include "window/window.h"

#include <SDL3/SDL.h>

#include <optional>
#include <utility>

namespace Spot::Detail {

namespace {

// The formats spot_add_shaders() produces; SDL picks a backend that accepts one of them. DXIL is only built where
// DXC is available, which is what Direct3D 12 needs.
#ifdef SPOT_SHADERS_DXIL
constexpr SDL_GPUShaderFormat ShaderFormats =
    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL;
#else
constexpr SDL_GPUShaderFormat ShaderFormats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
#endif

SDL_GPUPresentMode ChoosePresentMode(SDL_GPUDevice* device, SDL_Window* window, bool vsync) {
    if (!vsync) {
        // Mailbox avoids tearing without blocking; immediate is the fallback, and vsync is always supported.
        for (const auto mode : {SDL_GPU_PRESENTMODE_MAILBOX, SDL_GPU_PRESENTMODE_IMMEDIATE}) {
            if (SDL_WindowSupportsGPUPresentMode(device, window, mode)) {
                return mode;
            }
        }
    }
    return SDL_GPU_PRESENTMODE_VSYNC;
}

SDL_GPUDevice* CreateDevice(bool debug) {
    SDL_GPUDevice* device = SDL_CreateGPUDevice(ShaderFormats, debug, nullptr);
    if (!device && debug) {
        Log::GetEngineLogger().Warning("Failed to create a debug GPU device ({}); retrying without validation",
                                       SDL_GetError());
        device = SDL_CreateGPUDevice(ShaderFormats, false, nullptr);
    }
    return device;
}

} // namespace

SharedGpuDevice::SharedGpuDevice(SDL_GPUDevice* handle) : m_handle(handle) {
    // The device can outlive the window, so it keeps SDL running until it is destroyed.
    Platform::Initialize();
}

SharedGpuDevice::~SharedGpuDevice() {
    SDL_DestroyGPUDevice(m_handle);
    Platform::Shutdown();
}

SdlGpuDevice::SdlGpuDevice(std::shared_ptr<SharedGpuDevice> device, SDL_Window* window)
    : shared(std::move(device)), window(window) {
    // SDL_GPU has no GPU timers or bindless resources.
    capabilities.compressedTextures = SDL_GPUTextureSupportsFormat(
        shared->Get(), SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    capabilities.maxColorTargets = 1;
}

bool SdlGpuDevice::SupportsSampling(TextureFormat format) const {
    return SDL_GPUTextureSupportsFormat(shared->Get(), ToSdl(format), SDL_GPU_TEXTURETYPE_2D,
                                        SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

SdlGpuDevice::~SdlGpuDevice() {
    SDL_WaitForGPUIdle(shared->Get());
    if (window) {
        SDL_ReleaseWindowFromGPUDevice(shared->Get(), window);
    }
}

bool IsSdlGpuSupported() {
    // Probing a backend loads its driver, which needs the video subsystem running.
    if (!Platform::Initialize()) {
        return false;
    }
    const bool supported = SDL_GPUSupportsShaderFormats(ShaderFormats, nullptr);
    Platform::Shutdown();
    return supported;
}

std::unique_ptr<GraphicsDevice::Impl> CreateSdlGpuDevice(Window* window, const GraphicsConfig& config) {
    if (!window) {
        if (!Platform::Initialize()) {
            return nullptr;
        }
        SDL_GPUDevice* handle = CreateDevice(config.debug);
        if (!handle) {
            Log::GetEngineLogger().Error("Failed to create an offscreen GPU device: {}", SDL_GetError());
            Platform::Shutdown();
            return nullptr;
        }
        auto device = std::make_shared<SharedGpuDevice>(handle);
        Platform::Shutdown();
        auto impl = std::make_unique<SdlGpuDevice>(std::move(device), nullptr);
        impl->vsync = false;
        impl->composition =
            config.linearWindow ? SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR : SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
        return impl;
    }

    auto* sdlWindow = static_cast<SDL_Window*>(window->GetNativeHandle());
    SDL_GPUDevice* handle = CreateDevice(config.debug);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create GPU device: {}", SDL_GetError());
        return nullptr;
    }
    auto device = std::make_shared<SharedGpuDevice>(handle);

    if (!SDL_ClaimWindowForGPUDevice(handle, sdlWindow)) {
        Log::GetEngineLogger().Error("Failed to attach the window to the GPU device: {}", SDL_GetError());
        return nullptr;
    }

    auto impl = std::make_unique<SdlGpuDevice>(std::move(device), sdlWindow);
    // Rendering is linear, so the window's frames encode what shaders write as sRGB for display.
    if (!config.linearWindow) {
        impl->composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    } else if (SDL_WindowSupportsGPUSwapchainComposition(handle, sdlWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR)) {
        impl->composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR;
    } else {
        Log::GetEngineLogger().Warning("This GPU cannot show linear colors in the window, so they will look too dark");
    }
    impl->SetVSync(config.vsync);
    const char* adapter =
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(handle), SDL_PROP_GPU_DEVICE_NAME_STRING, "an unknown GPU");
    Log::GetEngineLogger().Info("Graphics device created with the '{}' backend on {}", impl->GetBackendName(), adapter);
    return impl;
}

bool SdlGpuDevice::SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const {
    const auto count = ToSdlSampleCount(sampleCount);
    return count && SDL_GPUTextureSupportsSampleCount(shared->Get(), ToSdl(format), *count);
}

bool SdlGpuDevice::SupportsRenderTarget(TextureFormat format) const {
    const SDL_GPUTextureUsageFlags usage =
        (IsDepthFormat(format) ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) |
        SDL_GPU_TEXTUREUSAGE_SAMPLER;
    return SDL_GPUTextureSupportsFormat(shared->Get(), ToSdl(format), SDL_GPU_TEXTURETYPE_2D, usage);
}

TextureFormat SdlGpuDevice::GetWindowFormat() const {
    if (!window) {
        return composition == SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8;
    }
    // The swapchain is SDR, linear or not, which SDL gives one of these formats.
    switch (SDL_GetGPUSwapchainTextureFormat(shared->Get(), window)) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return TextureFormat::Rgba8;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
        return TextureFormat::Rgba8Srgb;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
        return TextureFormat::Bgra8Srgb;
    default:
        return TextureFormat::Bgra8;
    }
}

std::string_view SdlGpuDevice::GetBackendName() const {
    return SDL_GetGPUDeviceDriver(shared->Get());
}

bool SdlGpuDevice::IsVSyncEnabled() const {
    return pendingVSync.value_or(vsync);
}

void SdlGpuDevice::SetVSync(bool enabled) {
    if (!window) {
        return;
    }
    // Changing the present mode recreates the swapchain, whose texture an open frame is still using.
    if (frameOpen) {
        pendingVSync = enabled;
        return;
    }

    const auto mode = ChoosePresentMode(shared->Get(), window, enabled);
    if (!SDL_SetGPUSwapchainParameters(shared->Get(), window, composition, mode)) {
        Log::GetEngineLogger().Error("Failed to change the present mode: {}", SDL_GetError());
        return;
    }
    vsync = mode == SDL_GPU_PRESENTMODE_VSYNC;
}

bool SdlGpuDevice::BeginFrame() {
    SPOT_ASSERT(!frameOpen, "BeginFrame() called while a frame is open");
    if (!window) {
        return false;
    }

    if (const auto pending = std::exchange(pendingVSync, std::nullopt)) {
        SetVSync(*pending);
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(shared->Get());
    if (!commandBuffer) {
        Log::GetEngineLogger().Error("Failed to acquire a command buffer: {}", SDL_GetError());
        return false;
    }

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &context.target, &context.width,
                                               &context.height)) {
        Log::GetEngineLogger().Error("Failed to acquire the swapchain texture: {}", SDL_GetError());
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return false;
    }

    // No texture means the window cannot be presented to right now, such as while it is minimized.
    if (!context.target) {
        SDL_SubmitGPUCommandBuffer(commandBuffer);
        return false;
    }

    context.commandBuffer = commandBuffer;
    context.acquiredTarget = context.target;
    context.format = GetWindowFormat();
    frameOpen = true;
    return true;
}

bool SdlGpuDevice::BeginOffscreenFrame(Texture& target) {
    SPOT_ASSERT(!frameOpen, "A frame is already open");
    if (!target.IsRenderTarget() || target.GetSampleCount() != 1 || target.GetLayerCount() != 1 ||
        IsDepthFormat(target.GetFormat())) {
        Log::GetEngineLogger().Error("Offscreen frames need a single-sample 2D color render target");
        return false;
    }
    auto* commandBuffer = SDL_AcquireGPUCommandBuffer(shared->Get());
    if (!commandBuffer) {
        Log::GetEngineLogger().Error("Failed to acquire an offscreen command buffer: {}", SDL_GetError());
        return false;
    }
    context.commandBuffer = commandBuffer;
    context.target = GetSdlTexture(target);
    context.width = target.GetWidth();
    context.height = target.GetHeight();
    context.acquiredTarget = context.target;
    context.format = target.GetFormat();
    frameOpen = true;
    return true;
}

void SdlGpuDevice::EndFrame() {
    SPOT_ASSERT(frameOpen, "EndFrame() called without an open frame");
    SPOT_ASSERT(!context.passOpen, "A render pass is still open at the end of the frame");

    if (!SDL_SubmitGPUCommandBuffer(context.commandBuffer)) {
        Log::GetEngineLogger().Error("Failed to submit the frame: {}", SDL_GetError());
    }
    context.commandBuffer = nullptr;
    context.target = nullptr;
    context.acquiredTarget = nullptr;
    context.width = 0;
    context.height = 0;
    context.passOpen = false;
    frameOpen = false;
}

} // namespace Spot::Detail
