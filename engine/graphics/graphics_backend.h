#pragma once

// Engine-internal: the contract every graphics backend implements. The public graphics classes hold an Impl that a
// backend derives from, and forward to it, so several backends can be built into one program and one is picked when
// the device is made. Public headers never include this file, so backend types never reach them.

#include "graphics/graphics_device.h"

#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Spot {

class Window;

struct Buffer::Impl {
    virtual ~Impl() = default;
    virtual void* GetNativeHandle() const = 0;

    BufferUsage usage = BufferUsage::Vertex;
    std::uint32_t size = 0;
};

struct Texture::Impl {
    virtual ~Impl() = default;
    virtual void* GetNativeHandle() const = 0;

    TextureDesc desc;
    std::uint32_t mipLevels = 1;
};

struct Sampler::Impl {
    virtual ~Impl() = default;
    virtual void* GetNativeHandle() const = 0;
};

struct Shader::Impl {
    virtual ~Impl() = default;
    virtual void* GetNativeHandle() const = 0;

    ShaderStage stage = ShaderStage::Vertex;
};

struct Pipeline::Impl {
    virtual ~Impl() = default;
    virtual void* GetNativeHandle() const = 0;
};

struct ComputePipeline::Impl {
    virtual ~Impl() = default;
    virtual void* GetNativeHandle() const = 0;
};

// An open compute pass; ComputePass forwards to it and checks how it is used.
struct ComputePass::Impl {
    virtual ~Impl() = default;

    virtual void End() = 0;
    virtual void BindPipeline(const ComputePipeline& pipeline) = 0;
    virtual void BindTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) = 0;
    virtual void BindStorageBuffer(std::uint32_t slot, const Buffer& buffer) = 0;
    virtual void PushUniformData(std::uint32_t slot, const void* data, std::uint32_t size) = 0;
    virtual void Dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) = 0;
    virtual void DispatchIndirect(const Buffer& buffer, std::uint32_t offset) = 0;
    virtual void* GetNativeHandle() const = 0;
};

// An open render pass; RenderPass forwards to it and checks how it is used.
struct RenderPass::Impl {
    virtual ~Impl() = default;

    virtual void End() = 0;
    virtual void BindPipeline(const Pipeline& pipeline) = 0;
    virtual void SetViewport(const Viewport& viewport) = 0;
    virtual void SetScissor(const ScissorRect& rect) = 0;
    virtual void BindVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint32_t offset) = 0;
    virtual void BindIndexBuffer(const Buffer& buffer, IndexType type, std::uint32_t offset) = 0;
    virtual void BindFragmentTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) = 0;
    virtual void BindVertexStorageBuffer(std::uint32_t slot, const Buffer& buffer) = 0;
    virtual void BindFragmentStorageBuffer(std::uint32_t slot, const Buffer& buffer) = 0;
    virtual void PushVertexUniformData(std::uint32_t slot, const void* data, std::uint32_t size) = 0;
    virtual void PushFragmentUniformData(std::uint32_t slot, const void* data, std::uint32_t size) = 0;
    virtual void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                      std::uint32_t firstInstance) = 0;
    virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                             std::int32_t vertexOffset, std::uint32_t firstInstance) = 0;
    virtual void DrawIndirect(const Buffer& buffer, std::uint32_t offset, std::uint32_t drawCount) = 0;
    virtual void DrawIndexedIndirect(const Buffer& buffer, std::uint32_t offset, std::uint32_t drawCount) = 0;
    virtual void* GetNativeHandle() const = 0;
};

// The frame being recorded.
struct RenderContext::Impl {
    virtual ~Impl() = default;

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    // Null when the backend could not begin the pass, after logging why.
    virtual std::unique_ptr<RenderPass::Impl> BeginRenderPass(const RenderPassDesc& desc) = 0;
    virtual std::unique_ptr<ComputePass::Impl> BeginComputePass(const ComputePassDesc& desc) = 0;
    virtual bool SetFrameTarget(Texture* target) = 0;
    virtual bool BlitTextureToFrame(const Texture& source) = 0;
    virtual void GenerateMipmaps(Texture& texture) = 0;
    virtual void* GetNativeHandle() const = 0;
};

struct GraphicsDevice::Impl {
    virtual ~Impl() = default;

    virtual std::string_view GetBackendName() const = 0;
    virtual const GraphicsCapabilities& GetCapabilities() const = 0;

    virtual std::unique_ptr<Shader> CreateShader(const ShaderDesc& desc) = 0;
    virtual std::unique_ptr<Pipeline> CreatePipeline(const PipelineDesc& desc) = 0;
    virtual std::unique_ptr<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
    virtual std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) = 0;
    virtual void UploadToBuffer(Buffer& buffer, std::span<const std::byte> data, std::uint32_t offset,
                                UploadMode mode) = 0;
    virtual std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) = 0;
    virtual std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc) = 0;
    // Replaces one mip level of every layer, then, when asked, makes the levels after it from it.
    virtual void UploadToTexture(Texture& texture, std::uint32_t mipLevel, std::span<const std::byte> pixels,
                                 bool generateMipmaps) = 0;
    virtual std::vector<std::uint8_t> DownloadTexture(const Texture& texture, std::uint32_t mipLevel) = 0;

    virtual bool SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const = 0;
    virtual bool SupportsRenderTarget(TextureFormat format) const = 0;
    virtual bool SupportsSampling(TextureFormat format) const = 0;
    virtual TextureFormat GetWindowFormat() const = 0;
    virtual bool IsVSyncEnabled() const = 0;
    virtual void SetVSync(bool enabled) = 0;

    // Whether a frame could begin; the context then records it until EndFrame().
    virtual bool BeginFrame() = 0;
    virtual bool BeginOffscreenFrame(Texture& target) = 0;
    virtual void EndFrame() = 0;
    virtual RenderContext::Impl& GetContext() = 0;
    virtual void* GetNativeHandle() const = 0;
};

namespace Detail {

// Lets backends make the public objects around their implementations, and reach the implementation of an object.
struct GraphicsAccess {
    template <typename T>
    static std::unique_ptr<T> Make(std::unique_ptr<typename T::Impl> impl) {
        return std::unique_ptr<T>(new T(std::move(impl)));
    }

    template <typename T>
    static typename T::Impl& GetImpl(const T& object) {
        return *object.m_impl;
    }
};

// The SDL_GPU backend: Direct3D 12, Vulkan or Metal through SDL. A null window makes an offscreen device. Null, after
// logging why, when no device can be made.
bool IsSdlGpuSupported();
std::unique_ptr<GraphicsDevice::Impl> CreateSdlGpuDevice(Window* window, const GraphicsConfig& config);

// The Vulkan backend, through NVRHI, where the engine was built with it.
bool IsNvrhiVulkanSupported();
std::unique_ptr<GraphicsDevice::Impl> CreateNvrhiVulkanDevice(Window* window, const GraphicsConfig& config);

} // namespace Detail

} // namespace Spot
