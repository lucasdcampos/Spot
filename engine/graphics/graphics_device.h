#pragma once

#include "graphics/buffer.h"
#include "graphics/compute.h"
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

// The graphics API the device runs on.
enum class GraphicsBackend {
    // The best the machine runs: Vulkan where the GPU runs Vulkan 1.3 and the build has it, SDL_GPU elsewhere.
    Auto,
    // SDL_GPU, over Direct3D 12, Vulkan or Metal.
    SdlGpu,
    // Vulkan through NVRHI, on Windows and Linux.
    Vulkan,
};

struct GraphicsConfig {
    // The environment variable SPOT_GRAPHICS_BACKEND, "sdl_gpu" or "vulkan", overrides Auto, as tests do to run on
    // either backend.
    GraphicsBackend backend = GraphicsBackend::Auto;
    // Turned off for runs without a display, such as tests or servers; the application then never renders.
    bool enabled = true;
    bool vsync = true;
    // Shaders write linear colors, which the window encodes as sRGB for display; the engine's renderers need this.
    // Turned off, the window shows what shaders write as it is, for drawing that blends in sRGB, as most UI does.
    bool linearWindow = true;
    // Enables the backend's validation layers, when installed. Slow, so only on by default in debug builds.
#ifdef NDEBUG
    bool debug = false;
#else
    bool debug = true;
#endif
};

// What the device's backend and GPU can do beyond what every device does. Features a device lacks are refused, after
// logging why, so code that uses them checks here first and takes another way.
struct GraphicsCapabilities {
    // Block-compressed texture formats (TextureFormat::Bc1 and after), which desktop GPUs have.
    bool compressedTextures = false;
    // Compute shaders, pipelines and passes (RenderContext::BeginComputePass()).
    bool compute = false;
    // Draws whose arguments a buffer holds (RenderPass::DrawIndirect()), and how many one call may draw.
    bool indirectDraws = false;
    std::uint32_t maxIndirectDrawCount = 0;
    // How many color targets a pass draws into at once (RenderPassDesc::additionalTargets), 1 without MRT.
    std::uint32_t maxColorTargets = 1;
    // GPU timers around parts of a frame (RenderContext::BeginGpuTimer()).
    bool timestamps = false;
    // Texture tables shaders index freely (TextureTable).
    bool bindlessTextures = false;
};

class GraphicsDevice {
public:
    // Whether this machine has a GPU and driver the backend can use.
    static bool IsSupported();

    // Returns null, after logging why, when no device can be created for the window.
    static std::unique_ptr<GraphicsDevice> Create(Window& window, const GraphicsConfig& config = {});
    // Creates a device without a window. Rendering and readback tests can use it without taking desktop focus.
    static std::unique_ptr<GraphicsDevice> CreateOffscreen(const GraphicsConfig& config = {});

    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    // The graphics API in use, such as "vulkan", "direct3d12" or "metal".
    std::string_view GetBackendName() const;
    const GraphicsCapabilities& GetCapabilities() const;

    // Resources may outlive the device; they keep the backend alive until they are destroyed.
    // Each returns null, after logging why, when the backend rejects the description.
    std::unique_ptr<Shader> CreateShader(const ShaderDesc& desc);
    std::unique_ptr<Pipeline> CreatePipeline(const PipelineDesc& desc);
    // Null, after logging why, where GraphicsCapabilities::compute is off.
    std::unique_ptr<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc);
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
    // Creates an RGBA texture the size of the image, holding its pixels. Images hold colors as they are displayed, so
    // they are sRGB by default; images of other data, such as coverage or normals, need a format that keeps it as is.
    std::unique_ptr<Texture> CreateTexture(const Image& image, bool mipmaps = false,
                                           TextureFormat format = TextureFormat::Rgba8Srgb);
    std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc = {});

    // Whether render targets of this format can have this many samples; 1 always can.
    bool SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const;
    // Whether textures of this format can be drawn into and sampled. Every uncompressed color format but
    // R11G11B10Float, Rg32Float, Rgba32Float and R32Uint always can; compressed ones never can.
    bool SupportsRenderTarget(TextureFormat format) const;
    // Whether textures of this format can be made and sampled at all: every format but the compressed ones and
    // R32Uint always can.
    bool SupportsSampling(TextureFormat format) const;
    // The format of the window's frames, which textures resolved into the window must have. It is sRGB where the GPU
    // supports it, as it nearly always does, so shaders write linear colors and the window shows them right, unless
    // GraphicsConfig::linearWindow is off.
    TextureFormat GetWindowFormat() const;

    // Replaces all of the texture's pixels: GetTextureDataSize() bytes, rows from top to bottom, for each of its layers
    // in turn (the six faces of a cube), and makes its other mip levels from them. Compressed textures have their
    // levels made beforehand instead, and uploaded with UploadToTextureLevel(). Like buffer uploads, it can be called
    // at any time.
    void UploadToTexture(Texture& texture, std::span<const std::byte> pixels);
    // Replaces the pixels of one mip level, of every layer in turn, leaving the other levels as they are.
    void UploadToTextureLevel(Texture& texture, std::uint32_t mipLevel, std::span<const std::byte> pixels);

    template <std::ranges::contiguous_range Data>
    void UploadToTexture(Texture& texture, const Data& pixels) {
        UploadToTexture(texture, std::span<const std::byte>(std::as_bytes(std::span(pixels))));
    }

    template <std::ranges::contiguous_range Data>
    void UploadToTextureLevel(Texture& texture, std::uint32_t mipLevel, const Data& pixels) {
        UploadToTextureLevel(texture, mipLevel, std::span<const std::byte>(std::as_bytes(std::span(pixels))));
    }

    // Reads the pixels of a mip level of the texture back, rows from top to bottom, for each of its layers in turn. It
    // waits for the GPU, so it is slow and meant for screenshots, tools and tests. It sees the work of frames already
    // submitted, not of the frame being recorded.
    std::vector<std::uint8_t> DownloadTexture(const Texture& texture, std::uint32_t mipLevel = 0);

    bool IsVSyncEnabled() const;
    void SetVSync(bool enabled);

    // Returns null when there is nothing to draw into this frame, such as while the window is minimized.
    // Every non-null frame must be finished with EndFrame(), which submits it to the GPU.
    RenderContext* BeginFrame();
    // Records a frame into this single-sample 2D color render target, which must stay alive until EndFrame().
    // Works with either kind of device. Default render passes draw into this target instead of the window.
    RenderContext* BeginOffscreenFrame(Texture& target);
    void EndFrame();

    // The backend's device (an SDL_GPUDevice* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    explicit GraphicsDevice(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
    std::unique_ptr<RenderContext> m_context;
};

} // namespace Spot
