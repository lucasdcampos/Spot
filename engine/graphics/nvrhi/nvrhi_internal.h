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
#include <unordered_map>
#include <utility>
#include <vector>

struct SDL_Window;

namespace Spot::Detail {

nvrhi::Format ToNvrhi(TextureFormat format);

class VulkanSurface;

// The Vulkan objects and the NVRHI device, shared by the graphics device and every resource made with it, so
// resources may outlive the graphics device, as the API promises: the device goes after the last of them.
struct NvrhiShared {
    NvrhiShared(std::unique_ptr<VulkanSurface> vulkan, nvrhi::vulkan::DeviceHandle vulkanDevice,
                nvrhi::DeviceHandle device);
    ~NvrhiShared();
    NvrhiShared(const NvrhiShared&) = delete;
    NvrhiShared& operator=(const NvrhiShared&) = delete;

    std::unique_ptr<VulkanSurface> vulkan;
    // NVRHI's Vulkan device, and the device the engine calls, which may be NVRHI's validation layer over it.
    nvrhi::vulkan::DeviceHandle vulkanDevice;
    nvrhi::DeviceHandle device;
};

struct NvrhiTexture final : Texture::Impl {
    void* GetNativeHandle() const override { return handle.Get(); }

    // First, so it goes after the handle.
    std::shared_ptr<NvrhiShared> shared;
    nvrhi::TextureHandle handle;
};

inline nvrhi::ITexture* GetNvrhiTexture(const Texture& texture) {
    return static_cast<nvrhi::ITexture*>(texture.GetNativeHandle());
}

struct NvrhiBuffer final : Buffer::Impl {
    void* GetNativeHandle() const override { return handle.Get(); }

    // First, so it goes after the handle.
    std::shared_ptr<NvrhiShared> shared;
    nvrhi::BufferHandle handle;
    // Memory the buffer moved away from by discarding uploads, with the frame that last read it, reused once the GPU
    // is done with that frame.
    std::vector<std::pair<nvrhi::BufferHandle, std::uint64_t>> retired;
};

inline nvrhi::IBuffer* GetNvrhiBuffer(const Buffer& buffer) {
    return static_cast<nvrhi::IBuffer*>(buffer.GetNativeHandle());
}

struct NvrhiSampler final : Sampler::Impl {
    void* GetNativeHandle() const override { return handle.Get(); }

    // First, so it goes after the handle.
    std::shared_ptr<NvrhiShared> shared;
    nvrhi::SamplerHandle handle;
};

inline nvrhi::ISampler* GetNvrhiSampler(const Sampler& sampler) {
    return static_cast<nvrhi::ISampler*>(sampler.GetNativeHandle());
}

// The resources a stage of a shader reads, as ShaderDesc counts them.
struct StageResources {
    std::uint32_t textures = 0;
    std::uint32_t storageBuffers = 0;
    std::uint32_t uniformBuffers = 0;
};

struct NvrhiShader final : Shader::Impl {
    void* GetNativeHandle() const override { return handle.Get(); }

    // First, so it goes after the handle.
    std::shared_ptr<NvrhiShared> shared;
    nvrhi::ShaderHandle handle;
    StageResources resources;
};

// Shaders follow SDL_GPU's register spaces, each a descriptor set: the vertex stage's textures, samplers and storage
// buffers, then its uniforms, then the fragment stage's.
enum class DescriptorSet : std::uint32_t {
    VertexResources,
    VertexUniforms,
    FragmentResources,
    FragmentUniforms,
    Count
};
constexpr std::size_t DescriptorSetCount = static_cast<std::size_t>(DescriptorSet::Count);

struct NvrhiPipeline final : Pipeline::Impl {
    void* GetNativeHandle() const override { return handle.Get(); }

    // First, so it goes after the handle.
    std::shared_ptr<NvrhiShared> shared;
    nvrhi::GraphicsPipelineHandle handle;
    std::array<nvrhi::BindingLayoutHandle, DescriptorSetCount> layouts;
    StageResources vertex;
    StageResources fragment;
};

// The most a stage binds of each kind; SDL_GPU's limits, which the engine's shaders stay within.
constexpr std::size_t MaxStageTextures = 16;
constexpr std::size_t MaxStageStorageBuffers = 8;
constexpr std::size_t MaxStageUniforms = 4;

class NvrhiContext final : public RenderContext::Impl {
public:
    // What draws read in each stage, as the pass set it: texture and sampler pairs, storage buffers, and uniforms in
    // volatile constant buffers the context owns.
    struct StageBindings {
        std::array<nvrhi::ITexture*, MaxStageTextures> textures = {};
        std::array<nvrhi::ISampler*, MaxStageTextures> samplers = {};
        std::array<nvrhi::IBuffer*, MaxStageStorageBuffers> storageBuffers = {};
        std::array<nvrhi::BufferHandle, MaxStageUniforms> uniforms = {};
    };

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

    std::array<StageBindings, 2> stages;
    // Writes uniforms into a volatile constant buffer of the stage's slot, grown when they outgrow it.
    void PushUniforms(std::size_t stage, std::uint32_t slot, const void* data, std::uint32_t size);
    // Binding sets and framebuffers made for draws, kept while they are used and let go a few frames after.
    nvrhi::IBindingSet* GetBindingSet(nvrhi::IBindingLayout* layout, const nvrhi::BindingSetDesc& desc);
    nvrhi::IFramebuffer* GetFramebuffer(const nvrhi::FramebufferDesc& desc);
    // Draws each level of the texture's mipmaps, of each face, from the level above it, into the command list.
    void RecordMipmaps(nvrhi::ICommandList& commands, Texture& texture);
    // Ends a frame for the caches, forgetting what was not used in the last frames.
    void FinishFrame();
    // Lets go of everything the context holds of the device, before the device goes.
    void Release();

private:
    struct CachedBindingSet {
        nvrhi::BindingSetHandle handle;
        std::uint64_t lastUsed = 0;
    };
    struct BindingSetKey {
        nvrhi::IBindingLayout* layout;
        nvrhi::BindingSetDesc desc;
        bool operator==(const BindingSetKey& other) const { return layout == other.layout && desc == other.desc; }
    };
    struct BindingSetKeyHash {
        std::size_t operator()(const BindingSetKey& key) const;
    };
    std::unordered_map<BindingSetKey, CachedBindingSet, BindingSetKeyHash> m_bindingSets;
    struct CachedFramebuffer {
        nvrhi::FramebufferHandle handle;
        std::uint64_t lastUsed = 0;
    };
    std::vector<std::pair<nvrhi::FramebufferDesc, CachedFramebuffer>> m_framebuffers;
    std::uint64_t m_frame = 1;

    // What mipmap generation draws with, made on first use: a pipeline per format, for 2D textures and for faces of
    // cubes, and descriptor sets 0 and 1 empty.
    nvrhi::IGraphicsPipeline* GetMipmapPipeline(nvrhi::Format targetFormat, bool layers);
    nvrhi::ShaderHandle m_mipVertexShader;
    std::array<nvrhi::ShaderHandle, 2> m_mipFragmentShaders;
    std::array<nvrhi::BindingLayoutHandle, 3> m_mipLayouts;
    nvrhi::SamplerHandle m_mipSampler;
    std::unordered_map<std::uint32_t, nvrhi::GraphicsPipelineHandle> m_mipPipelines;
};

class NvrhiDevice final : public GraphicsDevice::Impl {
public:
    // The device may be NVRHI's validation layer over the Vulkan device.
    explicit NvrhiDevice(std::shared_ptr<NvrhiShared> shared);
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
    void* GetNativeHandle() const override { return m_device; }

private:
    // Records commands into a command list of their own and runs it at once, so they reach the GPU before the frame
    // being recorded, as uploads made during a frame must.
    void RunNow(const std::function<void(nvrhi::ICommandList&)>& record);

    std::shared_ptr<NvrhiShared> m_shared;
    VulkanSurface* m_vulkan;
    nvrhi::vulkan::IDevice* m_vulkanDevice;
    nvrhi::IDevice* m_device;
    NvrhiContext m_context;
    // A query per frame in flight, so a frame waits for the GPU to finish the one that used its slot before.
    static constexpr std::size_t FramesInFlight = 2;
    std::array<nvrhi::EventQueryHandle, FramesInFlight> m_frameQueries;
    std::array<bool, FramesInFlight> m_frameQueried = {};
    std::size_t m_frameSlot = 0;
    // The frame being recorded, or next to be, counted from 1; frames before m_frameNumber - FramesInFlight are done.
    std::uint64_t m_frameNumber = 1;
    // Whether the open frame draws into the window, which it then presents, rather than into a texture.
    bool m_presenting = false;
};

} // namespace Spot::Detail
