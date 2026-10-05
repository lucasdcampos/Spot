#include "graphics/graphics_device.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/sdl_gpu/sdl_gpu_context.h"
#include "platform/platform.h"
#include "window/window.h"

#include <SDL3/SDL.h>

#include <optional>
#include <utility>

namespace Spot {

namespace {

// The shader formats the engine will ship; SDL picks a backend that accepts one of them.
constexpr SDL_GPUShaderFormat ShaderFormats =
    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;

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

struct GraphicsDevice::Impl {
    Impl(SDL_GPUDevice* device, SDL_Window* window, std::unique_ptr<RenderContext> context)
        : device(device), window(window), context(std::move(context)) {}

    ~Impl() {
        SDL_WaitForGPUIdle(device);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    SDL_GPUDevice* device;
    SDL_Window* window;
    bool vsync = true;
    std::optional<bool> pendingVSync;
    bool frameOpen = false;
    std::unique_ptr<RenderContext> context;
};

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

    SDL_GPUDevice* device = CreateDevice(config.debug);
    if (!device) {
        Log::GetEngineLogger().Error("Failed to create GPU device: {}", SDL_GetError());
        return nullptr;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, sdlWindow)) {
        Log::GetEngineLogger().Error("Failed to attach the window to the GPU device: {}", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        return nullptr;
    }

    auto context = std::unique_ptr<RenderContext>(new RenderContext());
    auto impl = std::make_unique<Impl>(device, sdlWindow, std::move(context));
    auto graphics = std::unique_ptr<GraphicsDevice>(new GraphicsDevice(std::move(impl)));
    graphics->SetVSync(config.vsync);
    Log::GetEngineLogger().Info("Graphics device created with the '{}' backend", graphics->GetBackendName());
    return graphics;
}

GraphicsDevice::GraphicsDevice(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

GraphicsDevice::~GraphicsDevice() = default;

std::string_view GraphicsDevice::GetBackendName() const {
    return SDL_GetGPUDeviceDriver(m_impl->device);
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

    const auto mode = ChoosePresentMode(m_impl->device, m_impl->window, enabled);
    if (!SDL_SetGPUSwapchainParameters(m_impl->device, m_impl->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
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

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(m_impl->device);
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
    return m_impl->device;
}

} // namespace Spot
