#pragma once

#include "graphics/render_context.h"

#include <memory>
#include <string_view>

namespace Spot {

class Window;

struct GraphicsConfig {
    // Turned off for runs without a display, such as tests or servers; the application then never renders.
    bool enabled = true;
    bool vsync = true;
    // Enables the backend's validation layers, when installed. Slow, so only on by default in debug builds.
#ifdef NDEBUG
    bool debug = false;
#else
    bool debug = true;
#endif
};

class GraphicsDevice {
public:
    // Whether this machine has a GPU and driver the backend can use.
    static bool IsSupported();

    // Returns null, after logging why, when no device can be created for the window.
    static std::unique_ptr<GraphicsDevice> Create(Window& window, const GraphicsConfig& config = {});

    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    // The graphics API in use, such as "vulkan", "direct3d12" or "metal".
    std::string_view GetBackendName() const;

    bool IsVSyncEnabled() const;
    void SetVSync(bool enabled);

    // Returns null when there is nothing to draw into this frame, such as while the window is minimized.
    // Every non-null frame must be finished with EndFrame(), which submits it to the GPU.
    RenderContext* BeginFrame();
    void EndFrame();

    // The backend's device (an SDL_GPUDevice* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

private:
    struct Impl;

    explicit GraphicsDevice(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
