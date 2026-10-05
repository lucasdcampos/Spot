#pragma once

#include "graphics/buffer.h"
#include "graphics/image.h"
#include "graphics/pipeline.h"
#include "graphics/render_context.h"
#include "graphics/shader.h"
#include "graphics/texture.h"

#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

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

    // Resources may outlive the device; they keep the backend alive until they are destroyed.
    // Each returns null, after logging why, when the backend rejects the description.
    std::unique_ptr<Shader> CreateShader(const ShaderDesc& desc);
    std::unique_ptr<Pipeline> CreatePipeline(const PipelineDesc& desc);
    std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc);

    // Creates a buffer sized to hold the data, such as a std::array or std::vector of vertices, and uploads it.
    template <std::ranges::contiguous_range Data>
    std::unique_ptr<Buffer> CreateBuffer(BufferUsage usage, const Data& data) {
        const std::span<const std::byte> bytes = std::as_bytes(std::span(data));
        auto buffer = CreateBuffer({.usage = usage, .size = static_cast<std::uint32_t>(bytes.size())});
        if (buffer) {
            UploadToBuffer(*buffer, bytes);
        }
        return buffer;
    }

    // Copies data into the buffer at the given byte offset. It can be called at any time, including during a frame:
    // the copy reaches the GPU before the frame's draws.
    void UploadToBuffer(Buffer& buffer, std::span<const std::byte> data, std::uint32_t offset = 0,
                        UploadMode mode = UploadMode::Preserve);

    template <std::ranges::contiguous_range Data>
    void UploadToBuffer(Buffer& buffer, const Data& data, std::uint32_t offset = 0,
                        UploadMode mode = UploadMode::Preserve) {
        // Converted to a dynamic extent so the byte overload is chosen instead of this template again.
        UploadToBuffer(buffer, std::span<const std::byte>(std::as_bytes(std::span(data))), offset, mode);
    }

    std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc);
    // Creates an RGBA texture the size of the image, holding its pixels.
    std::unique_ptr<Texture> CreateTexture(const Image& image, bool mipmaps = false);
    std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc = {});

    // Whether render targets of this format can have this many samples; 1 always can.
    bool SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const;
    // The format of the window's frames, which textures resolved into the window must have.
    TextureFormat GetWindowFormat() const;

    // Replaces all of the texture's pixels: width * height * 4 bytes, rows from top to bottom. Like buffer uploads,
    // it can be called at any time.
    void UploadToTexture(Texture& texture, std::span<const std::byte> pixels);

    template <std::ranges::contiguous_range Data>
    void UploadToTexture(Texture& texture, const Data& pixels) {
        UploadToTexture(texture, std::span<const std::byte>(std::as_bytes(std::span(pixels))));
    }

    // Reads the texture's pixels back, rows from top to bottom. It waits for the GPU, so it is slow and meant for
    // screenshots, tools and tests. It sees the work of frames already submitted, not of the frame being recorded.
    std::vector<std::uint8_t> DownloadTexture(const Texture& texture);

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
