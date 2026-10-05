#include "graphics/graphics_device.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/sdl_gpu/sdl_gpu_internal.h"
#include "platform/platform.h"
#include "window/window.h"

#include <SDL3/SDL.h>

#include <optional>
#include <utility>

namespace Spot {

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

GraphicsDevice::Impl::Impl(std::shared_ptr<SharedGpuDevice> device, SDL_Window* window,
                           std::unique_ptr<RenderContext> context)
    : device(std::move(device)), window(window), context(std::move(context)) {
}

GraphicsDevice::Impl::~Impl() {
    SDL_WaitForGPUIdle(device->Get());
    SDL_ReleaseWindowFromGPUDevice(device->Get(), window);
}

bool GraphicsDevice::IsSupported() {
    // Probing a backend loads its driver, which needs the video subsystem running.
    if (!Platform::Initialize()) {
        return false;
    }
    const bool supported = SDL_GPUSupportsShaderFormats(ShaderFormats, nullptr);
    Platform::Shutdown();
    return supported;
}

std::unique_ptr<GraphicsDevice> GraphicsDevice::Create(Window& window, const GraphicsConfig& config) {
    auto* sdlWindow = static_cast<SDL_Window*>(window.GetNativeHandle());

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

    auto context = std::unique_ptr<RenderContext>(new RenderContext());
    auto impl = std::make_unique<Impl>(std::move(device), sdlWindow, std::move(context));
    // Rendering is linear, so the window's frames encode what shaders write as sRGB for display.
    if (SDL_WindowSupportsGPUSwapchainComposition(handle, sdlWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR)) {
        impl->composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR;
    } else {
        Log::GetEngineLogger().Warning("This GPU cannot show linear colors in the window, so they will look too dark");
    }
    auto graphics = std::unique_ptr<GraphicsDevice>(new GraphicsDevice(std::move(impl)));
    graphics->SetVSync(config.vsync);
    const char* adapter = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(graphics->m_impl->device->Get()),
                                                SDL_PROP_GPU_DEVICE_NAME_STRING, "an unknown GPU");
    Log::GetEngineLogger().Info("Graphics device created with the '{}' backend on {}", graphics->GetBackendName(),
                                adapter);
    return graphics;
}

GraphicsDevice::GraphicsDevice(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

GraphicsDevice::~GraphicsDevice() = default;

bool GraphicsDevice::SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const {
    const auto count = ToSdlSampleCount(sampleCount);
    return count && SDL_GPUTextureSupportsSampleCount(m_impl->device->Get(), ToSdl(format), *count);
}

TextureFormat GraphicsDevice::GetWindowFormat() const {
    // The swapchain is SDR, linear or not, which SDL gives one of these formats.
    switch (SDL_GetGPUSwapchainTextureFormat(m_impl->device->Get(), m_impl->window)) {
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

std::string_view GraphicsDevice::GetBackendName() const {
    return SDL_GetGPUDeviceDriver(m_impl->device->Get());
}

bool GraphicsDevice::IsVSyncEnabled() const {
    return m_impl->pendingVSync.value_or(m_impl->vsync);
}

void GraphicsDevice::SetVSync(bool enabled) {
    // Changing the present mode recreates the swapchain, whose texture an open frame is still using.
    if (m_impl->frameOpen) {
        m_impl->pendingVSync = enabled;
        return;
    }

    const auto mode = ChoosePresentMode(m_impl->device->Get(), m_impl->window, enabled);
    if (!SDL_SetGPUSwapchainParameters(m_impl->device->Get(), m_impl->window, m_impl->composition, mode)) {
        Log::GetEngineLogger().Error("Failed to change the present mode: {}", SDL_GetError());
        return;
    }
    m_impl->vsync = mode == SDL_GPU_PRESENTMODE_VSYNC;
}

RenderContext* GraphicsDevice::BeginFrame() {
    SPOT_ASSERT(!m_impl->frameOpen, "BeginFrame() called while a frame is open");

    if (const auto vsync = std::exchange(m_impl->pendingVSync, std::nullopt)) {
        SetVSync(*vsync);
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(m_impl->device->Get());
    if (!commandBuffer) {
        Log::GetEngineLogger().Error("Failed to acquire a command buffer: {}", SDL_GetError());
        return nullptr;
    }

    auto& context = *m_impl->context->m_impl;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, m_impl->window, &context.target, &context.width,
                                               &context.height)) {
        Log::GetEngineLogger().Error("Failed to acquire the swapchain texture: {}", SDL_GetError());
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return nullptr;
    }

    // No texture means the window cannot be presented to right now, such as while it is minimized.
    if (!context.target) {
        SDL_SubmitGPUCommandBuffer(commandBuffer);
        return nullptr;
    }

    context.commandBuffer = commandBuffer;
    m_impl->frameOpen = true;
    return m_impl->context.get();
}

void GraphicsDevice::EndFrame() {
    SPOT_ASSERT(m_impl->frameOpen, "EndFrame() called without an open frame");

    auto& context = *m_impl->context->m_impl;
    SPOT_ASSERT(!context.passOpen, "A render pass is still open at the end of the frame");

    if (!SDL_SubmitGPUCommandBuffer(context.commandBuffer)) {
        Log::GetEngineLogger().Error("Failed to submit the frame: {}", SDL_GetError());
    }
    context = {};
    m_impl->frameOpen = false;
}

void* GraphicsDevice::GetNativeHandle() const {
    return m_impl->device->Get();
}

} // namespace Spot
