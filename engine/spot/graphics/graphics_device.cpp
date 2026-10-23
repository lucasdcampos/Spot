#include "spot/graphics/graphics_device.h"

#include "spot/core/assert.h"
#include "spot/core/log.h"
#include "spot/graphics/graphics_backend.h"
#include "spot/platform/process.h"

#include <algorithm>
#include <utility>

namespace Spot {

namespace {

// The backend asked for, by the config or else by SPOT_GRAPHICS_BACKEND; Auto when neither asks for one.
GraphicsBackend ChooseBackend(GraphicsBackend requested) {
    if (requested != GraphicsBackend::Auto) {
        return requested;
    }
    const auto chosen = Process::GetEnvironmentValue("SPOT_GRAPHICS_BACKEND");
    if (chosen == "vulkan") {
        return GraphicsBackend::Vulkan;
    }
    if (chosen == "sdl_gpu") {
        return GraphicsBackend::SdlGpu;
    }
    if (chosen) {
        Log::GetEngineLogger().Warning("SPOT_GRAPHICS_BACKEND '{}' is not sdl_gpu or vulkan; choosing one", *chosen);
    }
    return GraphicsBackend::Auto;
}

std::unique_ptr<GraphicsDevice::Impl> CreateBackend(Window* window, const GraphicsConfig& config) {
    switch (ChooseBackend(config.backend)) {
    case GraphicsBackend::Vulkan:
        return Detail::CreateNvrhiVulkanDevice(window, config);
    case GraphicsBackend::SdlGpu:
        return Detail::CreateSdlGpuDevice(window, config);
    case GraphicsBackend::Auto:
        break;
    }
    // Vulkan through NVRHI where it is built and the GPU runs Vulkan 1.3, as it does more; SDL_GPU everywhere else,
    // such as on macOS or older GPUs and drivers.
    if (Detail::IsNvrhiVulkanSupported()) {
        if (auto vulkan = Detail::CreateNvrhiVulkanDevice(window, config)) {
            return vulkan;
        }
        Log::GetEngineLogger().Warning("The Vulkan backend could not start; falling back to SDL_GPU");
    }
    return Detail::CreateSdlGpuDevice(window, config);
}

} // namespace

#ifndef SPOT_GRAPHICS_NVRHI
// Builds without NVRHI, as on macOS, have no Vulkan backend.
bool Detail::IsNvrhiVulkanSupported() {
    return false;
}

std::unique_ptr<GraphicsDevice::Impl> Detail::CreateNvrhiVulkanDevice(Window*, const GraphicsConfig&) {
    Log::GetEngineLogger().Error("This build of the engine has no Vulkan backend");
    return nullptr;
}
#endif

bool GraphicsDevice::IsSupported() {
    return Detail::IsSdlGpuSupported() || Detail::IsNvrhiVulkanSupported();
}

std::unique_ptr<GraphicsDevice> GraphicsDevice::Create(Window& window, const GraphicsConfig& config) {
    auto impl = CreateBackend(&window, config);
    return impl ? std::unique_ptr<GraphicsDevice>(new GraphicsDevice(std::move(impl))) : nullptr;
}

std::unique_ptr<GraphicsDevice> GraphicsDevice::CreateOffscreen(const GraphicsConfig& config) {
    auto impl = CreateBackend(nullptr, config);
    return impl ? std::unique_ptr<GraphicsDevice>(new GraphicsDevice(std::move(impl))) : nullptr;
}

GraphicsDevice::GraphicsDevice(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl)), m_context(new RenderContext(m_impl->GetContext(), m_impl->GetCapabilities())) {
}

GraphicsDevice::~GraphicsDevice() = default;

std::string_view GraphicsDevice::GetBackendName() const {
    return m_impl->GetBackendName();
}

std::unique_ptr<Shader> GraphicsDevice::CreateShader(const ShaderDesc& desc) {
    return m_impl->CreateShader(desc);
}

std::unique_ptr<Pipeline> GraphicsDevice::CreatePipeline(const PipelineDesc& desc) {
    if (desc.textureTable && !GetCapabilities().bindlessTextures) {
        Log::GetEngineLogger().Error("The '{}' backend has no texture tables", GetBackendName());
        return nullptr;
    }
    SPOT_ASSERT(desc.vertexShader && desc.vertexShader->GetStage() == ShaderStage::Vertex,
                "A pipeline needs a vertex shader");
    SPOT_ASSERT(desc.fragmentShader && desc.fragmentShader->GetStage() == ShaderStage::Fragment,
                "A pipeline needs a fragment shader");
    SPOT_ASSERT(desc.depth || !desc.depthOnly, "Depth-only pipelines need a depth state");
    SPOT_ASSERT(!desc.depth || IsDepthFormat(desc.depth->format), "Depth states need a depth format");
    if (desc.additionalTargetFormats.size() + 1 > GetCapabilities().maxColorTargets) {
        Log::GetEngineLogger().Error("This device draws into {} color targets at most, not {}",
                                     GetCapabilities().maxColorTargets, desc.additionalTargetFormats.size() + 1);
        return nullptr;
    }
    return m_impl->CreatePipeline(desc);
}

std::unique_ptr<ComputePipeline> GraphicsDevice::CreateComputePipeline(const ComputePipelineDesc& desc) {
    SPOT_ASSERT(desc.shader && desc.shader->GetStage() == ShaderStage::Compute,
                "A compute pipeline needs a compute shader");
    if (!GetCapabilities().compute) {
        Log::GetEngineLogger().Error("The '{}' backend runs no compute shaders", GetBackendName());
        return nullptr;
    }
    return m_impl->CreateComputePipeline(desc);
}

std::unique_ptr<Buffer> GraphicsDevice::CreateBuffer(const BufferDesc& desc) {
    return m_impl->CreateBuffer(desc);
}

void GraphicsDevice::UploadToBuffer(Buffer& buffer, std::span<const std::byte> data, std::uint32_t offset,
                                    UploadMode mode) {
    SPOT_ASSERT(offset + data.size() <= buffer.GetSize(), "Upload of {} bytes at offset {} overflows a {}-byte buffer",
                data.size(), offset, buffer.GetSize());
    if (!data.empty()) {
        m_impl->UploadToBuffer(buffer, data, offset, mode);
    }
}

std::unique_ptr<Texture> GraphicsDevice::CreateTexture(const TextureDesc& desc) {
    if (desc.storage && (!GetCapabilities().compute || desc.type != TextureType::Texture2D ||
                         IsDepthFormat(desc.format) || IsCompressedFormat(desc.format) || desc.sampleCount != 1)) {
        Log::GetEngineLogger().Error(
            "Storage textures are single-sample 2D color textures, on devices that run compute shaders");
        return nullptr;
    }
    if (IsCompressedFormat(desc.format)) {
        if (desc.renderTarget || desc.sampleCount != 1) {
            Log::GetEngineLogger().Error("Compressed textures cannot be render targets or multisampled");
            return nullptr;
        }
        if (!SupportsSampling(desc.format)) {
            Log::GetEngineLogger().Error("This GPU has no compressed texture format {}", static_cast<int>(desc.format));
            return nullptr;
        }
    }
    return m_impl->CreateTexture(desc);
}

std::unique_ptr<Texture> GraphicsDevice::CreateTexture(const Image& image, bool mipmaps, TextureFormat format) {
    auto texture =
        CreateTexture({.width = image.GetWidth(), .height = image.GetHeight(), .format = format, .mipmaps = mipmaps});
    if (texture) {
        UploadToTexture(*texture, image.GetPixels());
    }
    return texture;
}

std::unique_ptr<Sampler> GraphicsDevice::CreateSampler(const SamplerDesc& desc) {
    return m_impl->CreateSampler(desc);
}

std::unique_ptr<TextureTable> GraphicsDevice::CreateTextureTable(std::uint32_t capacity) {
    if (!GetCapabilities().bindlessTextures) {
        Log::GetEngineLogger().Error("The '{}' backend has no texture tables", GetBackendName());
        return nullptr;
    }
    if (capacity == 0 || capacity > MaxTextureTableCapacity) {
        Log::GetEngineLogger().Error("Texture tables hold 1 to {} textures, not {}", MaxTextureTableCapacity, capacity);
        return nullptr;
    }
    return m_impl->CreateTextureTable(capacity);
}

void GraphicsDevice::SetTextureTableEntry(TextureTable& table, std::uint32_t index, const Texture& texture) {
    SPOT_ASSERT(index < table.GetCapacity(), "Index {} is past the end of a table of {} textures", index,
                table.GetCapacity());
    SPOT_ASSERT(texture.GetType() == TextureType::Texture2D && texture.GetSampleCount() == 1,
                "Texture tables hold single-sample 2D textures");
    m_impl->SetTextureTableEntry(table, index, texture);
}

void GraphicsDevice::UploadToTexture(Texture& texture, std::span<const std::byte> pixels) {
    SPOT_ASSERT(!IsDepthFormat(texture.GetFormat()), "Depth textures cannot be uploaded to");
    SPOT_ASSERT(texture.GetSampleCount() == 1, "Multisampled textures cannot be uploaded to");
    const std::uint32_t size =
        GetTextureDataSize(texture.GetFormat(), texture.GetWidth(), texture.GetHeight()) * texture.GetLayerCount();
    SPOT_ASSERT(pixels.size() == size, "A {}x{} texture takes {} bytes of pixels, not {}", texture.GetWidth(),
                texture.GetHeight(), size, pixels.size());
    m_impl->UploadToTexture(texture, 0, pixels,
                            texture.GetMipLevelCount() > 1 && !IsCompressedFormat(texture.GetFormat()));
}

void GraphicsDevice::UploadToTextureLevel(Texture& texture, std::uint32_t mipLevel, std::span<const std::byte> pixels) {
    SPOT_ASSERT(!IsDepthFormat(texture.GetFormat()), "Depth textures cannot be uploaded to");
    SPOT_ASSERT(texture.GetSampleCount() == 1, "Multisampled textures cannot be uploaded to");
    SPOT_ASSERT(mipLevel < texture.GetMipLevelCount(), "The texture has no mip level {}", mipLevel);
    const std::uint32_t width = std::max(texture.GetWidth() >> mipLevel, 1u);
    const std::uint32_t height = std::max(texture.GetHeight() >> mipLevel, 1u);
    const std::uint32_t size = GetTextureDataSize(texture.GetFormat(), width, height) * texture.GetLayerCount();
    SPOT_ASSERT(pixels.size() == size, "Mip level {} of the texture takes {} bytes, not {}", mipLevel, size,
                pixels.size());
    m_impl->UploadToTexture(texture, mipLevel, pixels, false);
}

std::vector<std::uint8_t> GraphicsDevice::DownloadTexture(const Texture& texture, std::uint32_t mipLevel) {
    SPOT_ASSERT(!IsDepthFormat(texture.GetFormat()), "Depth textures cannot be downloaded");
    SPOT_ASSERT(texture.GetSampleCount() == 1, "Multisampled textures cannot be downloaded; resolve them first");
    SPOT_ASSERT(mipLevel < texture.GetMipLevelCount(), "The texture has no mip level {}", mipLevel);
    return m_impl->DownloadTexture(texture, mipLevel);
}

bool GraphicsDevice::SupportsSampleCount(TextureFormat format, std::uint32_t sampleCount) const {
    return m_impl->SupportsSampleCount(format, sampleCount);
}

bool GraphicsDevice::SupportsRenderTarget(TextureFormat format) const {
    return !IsCompressedFormat(format) && m_impl->SupportsRenderTarget(format);
}

bool GraphicsDevice::SupportsSampling(TextureFormat format) const {
    return (!IsCompressedFormat(format) || GetCapabilities().compressedTextures) && m_impl->SupportsSampling(format);
}

const GraphicsCapabilities& GraphicsDevice::GetCapabilities() const {
    return m_impl->GetCapabilities();
}

const std::vector<GpuTiming>& GraphicsDevice::GetGpuTimings() const {
    return m_impl->GetGpuTimings();
}

TextureFormat GraphicsDevice::GetWindowFormat() const {
    return m_impl->GetWindowFormat();
}

bool GraphicsDevice::IsVSyncEnabled() const {
    return m_impl->IsVSyncEnabled();
}

void GraphicsDevice::SetVSync(bool enabled) {
    m_impl->SetVSync(enabled);
}

RenderContext* GraphicsDevice::BeginFrame() {
    return m_impl->BeginFrame() ? m_context.get() : nullptr;
}

RenderContext* GraphicsDevice::BeginOffscreenFrame(Texture& target) {
    return m_impl->BeginOffscreenFrame(target) ? m_context.get() : nullptr;
}

void GraphicsDevice::EndFrame() {
    m_impl->EndFrame();
}

void* GraphicsDevice::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}

RenderContext::RenderContext(Impl& impl, const GraphicsCapabilities& capabilities)
    : m_impl(&impl), m_capabilities(&capabilities) {
}

RenderContext::~RenderContext() = default;

int RenderContext::GetWidth() const {
    return m_impl->GetWidth();
}

int RenderContext::GetHeight() const {
    return m_impl->GetHeight();
}

RenderPass RenderContext::BeginRenderPass(const RenderPassDesc& desc) {
    SPOT_ASSERT(!desc.depthOnly || (desc.depthTarget && !desc.target && !desc.clearColor && !desc.resolveTarget &&
                                    !desc.resolveToWindow),
                "Depth-only passes need a depth target and nothing to draw colors into");
    SPOT_ASSERT(!desc.target || desc.target->IsRenderTarget(), "Render pass targets must be created as render targets");
    SPOT_ASSERT(!desc.target || !IsDepthFormat(desc.target->GetFormat()),
                "Depth textures go in RenderPassDesc::depthTarget");
    SPOT_ASSERT((desc.targetLayer == 0 && desc.targetMipLevel == 0) ||
                    (desc.target && desc.targetLayer < desc.target->GetLayerCount() &&
                     desc.targetMipLevel < desc.target->GetMipLevelCount()),
                "The pass's target has no layer {} or mip level {}", desc.targetLayer, desc.targetMipLevel);
    SPOT_ASSERT(!(desc.resolveTarget && desc.resolveToWindow), "A pass resolves into one texture or the window");
    SPOT_ASSERT((desc.resolveTarget || desc.resolveToWindow) == (desc.target && desc.target->GetSampleCount() > 1),
                "Multisampled targets must be resolved, and only they can be");
    SPOT_ASSERT(!desc.resolveTarget ||
                    (desc.resolveTarget->IsRenderTarget() && desc.resolveTarget->GetSampleCount() == 1 &&
                     desc.resolveTarget->GetFormat() == desc.target->GetFormat() &&
                     desc.resolveTarget->GetWidth() == desc.target->GetWidth() &&
                     desc.resolveTarget->GetHeight() == desc.target->GetHeight()),
                "Resolve targets must be single-sample render targets of the size and format of the target");
    SPOT_ASSERT(!desc.depthTarget ||
                    (desc.depthTarget->IsRenderTarget() && IsDepthFormat(desc.depthTarget->GetFormat())),
                "Depth targets must be depth textures created as render targets");
    SPOT_ASSERT(desc.additionalTargets.empty() || (desc.target && !desc.depthOnly),
                "Passes with several color targets name their first");
    for (const ColorTarget& color : desc.additionalTargets) {
        SPOT_ASSERT(color.texture && color.texture->IsRenderTarget() && !IsDepthFormat(color.texture->GetFormat()),
                    "Color targets must be color textures created as render targets");
        SPOT_ASSERT(std::max(color.texture->GetWidth() >> color.mipLevel, 1u) ==
                            std::max(desc.target->GetWidth() >> desc.targetMipLevel, 1u) &&
                        std::max(color.texture->GetHeight() >> color.mipLevel, 1u) ==
                            std::max(desc.target->GetHeight() >> desc.targetMipLevel, 1u) &&
                        color.texture->GetSampleCount() == desc.target->GetSampleCount(),
                    "Color targets must have the size and sample count of the first");
        SPOT_ASSERT((color.resolveTarget != nullptr) == (color.texture->GetSampleCount() > 1),
                    "Multisampled color targets must be resolved, and only they can be");
    }
    if (desc.additionalTargets.size() + 1 > m_capabilities->maxColorTargets) {
        Log::GetEngineLogger().Error("This device draws into {} color targets at most, not {}",
                                     m_capabilities->maxColorTargets, desc.additionalTargets.size() + 1);
        return RenderPass(nullptr);
    }
    return RenderPass(m_impl->BeginRenderPass(desc));
}

ComputePass RenderContext::BeginComputePass(const ComputePassDesc& desc) {
    for (const StorageTexture& storage : desc.textures) {
        SPOT_ASSERT(storage.texture && storage.texture->IsStorage() &&
                        storage.mipLevel < storage.texture->GetMipLevelCount(),
                    "Compute passes write mip levels of textures created with TextureDesc::storage");
    }
    for (const Buffer* buffer : desc.buffers) {
        SPOT_ASSERT(buffer &&
                        (buffer->GetUsage() == BufferUsage::Storage || buffer->GetUsage() == BufferUsage::Indirect),
                    "Compute passes write storage and indirect buffers");
    }
    if (!m_capabilities->compute) {
        Log::GetEngineLogger().Error("This device runs no compute shaders");
        return ComputePass(nullptr);
    }
    return ComputePass(m_impl->BeginComputePass(desc));
}

bool RenderContext::SetFrameTarget(Texture* target) {
    return m_impl->SetFrameTarget(target);
}

bool RenderContext::BlitTextureToFrame(const Texture& source) {
    return m_impl->BlitTextureToFrame(source);
}

void RenderContext::GenerateMipmaps(Texture& texture) {
    m_impl->GenerateMipmaps(texture);
}

void RenderContext::BeginGpuTimer(std::string_view name) {
    if (m_capabilities->timestamps) {
        m_impl->BeginGpuTimer(name);
    }
}

void RenderContext::EndGpuTimer() {
    if (m_capabilities->timestamps) {
        m_impl->EndGpuTimer();
    }
}

void* RenderContext::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}

RenderPass::RenderPass(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

RenderPass::~RenderPass() {
    End();
}

void RenderPass::End() {
    if (m_impl) {
        m_impl->End();
        m_impl.reset();
    }
}

void RenderPass::BindPipeline(const Pipeline& pipeline) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    m_impl->BindPipeline(pipeline);
    m_pipelineBound = true;
}

void RenderPass::SetViewport(const Viewport& viewport) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    m_impl->SetViewport(viewport);
}

void RenderPass::SetScissor(const ScissorRect& rect) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(rect.width >= 0 && rect.height >= 0, "Scissor rectangles cannot have a negative size");
    m_impl->SetScissor(rect);
}

void RenderPass::BindVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint32_t offset) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Vertex, "Only vertex buffers can be bound as vertex buffers");
    m_impl->BindVertexBuffer(slot, buffer, offset);
}

void RenderPass::BindIndexBuffer(const Buffer& buffer, IndexType type, std::uint32_t offset) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Index, "Only index buffers can be bound as index buffers");
    m_impl->BindIndexBuffer(buffer, type, offset);
}

void RenderPass::BindFragmentTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    m_impl->BindFragmentTexture(slot, texture, sampler);
}

void RenderPass::BindVertexStorageBuffer(std::uint32_t slot, const Buffer& buffer) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Storage, "Only storage buffers can be bound as storage buffers");
    m_impl->BindVertexStorageBuffer(slot, buffer);
}

void RenderPass::BindFragmentStorageBuffer(std::uint32_t slot, const Buffer& buffer) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Storage, "Only storage buffers can be bound as storage buffers");
    m_impl->BindFragmentStorageBuffer(slot, buffer);
}

void RenderPass::BindTextureTable(const TextureTable& table) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    m_impl->BindTextureTable(table);
}

void RenderPass::PushVertexUniformData(std::uint32_t slot, const void* data, std::uint32_t size) {
    if (m_impl) {
        m_impl->PushVertexUniformData(slot, data, size);
    }
}

void RenderPass::PushFragmentUniformData(std::uint32_t slot, const void* data, std::uint32_t size) {
    if (m_impl) {
        m_impl->PushFragmentUniformData(slot, data, size);
    }
}

void RenderPass::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                      std::uint32_t firstInstance) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before drawing");
    m_impl->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void RenderPass::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                             std::int32_t vertexOffset, std::uint32_t firstInstance) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before drawing");
    m_impl->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void RenderPass::DrawIndirect(const Buffer& buffer, std::uint32_t offset, std::uint32_t drawCount) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before drawing");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Indirect, "Indirect draws read indirect buffers");
    SPOT_ASSERT(offset + drawCount * sizeof(DrawIndirectArguments) <= buffer.GetSize(),
                "The draws' arguments overflow the indirect buffer");
    if (drawCount > 0) {
        m_impl->DrawIndirect(buffer, offset, drawCount);
    }
}

void RenderPass::DrawIndexedIndirect(const Buffer& buffer, std::uint32_t offset, std::uint32_t drawCount) {
    SPOT_ASSERT(m_impl, "The render pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before drawing");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Indirect, "Indirect draws read indirect buffers");
    SPOT_ASSERT(offset + drawCount * sizeof(DrawIndexedIndirectArguments) <= buffer.GetSize(),
                "The draws' arguments overflow the indirect buffer");
    if (drawCount > 0) {
        m_impl->DrawIndexedIndirect(buffer, offset, drawCount);
    }
}

void* RenderPass::GetNativeHandle() const {
    return m_impl ? m_impl->GetNativeHandle() : nullptr;
}

ComputePass::ComputePass(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

ComputePass::~ComputePass() {
    End();
}

void ComputePass::End() {
    if (m_impl) {
        m_impl->End();
        m_impl.reset();
    }
}

void ComputePass::BindPipeline(const ComputePipeline& pipeline) {
    SPOT_ASSERT(m_impl, "The compute pass has ended");
    m_impl->BindPipeline(pipeline);
    m_pipelineBound = true;
}

void ComputePass::BindTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) {
    SPOT_ASSERT(m_impl, "The compute pass has ended");
    m_impl->BindTexture(slot, texture, sampler);
}

void ComputePass::BindStorageBuffer(std::uint32_t slot, const Buffer& buffer) {
    SPOT_ASSERT(m_impl, "The compute pass has ended");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Storage || buffer.GetUsage() == BufferUsage::Indirect,
                "Only storage and indirect buffers can be bound as storage buffers");
    m_impl->BindStorageBuffer(slot, buffer);
}

void ComputePass::PushUniformData(std::uint32_t slot, const void* data, std::uint32_t size) {
    if (m_impl) {
        m_impl->PushUniformData(slot, data, size);
    }
}

void ComputePass::Dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) {
    SPOT_ASSERT(m_impl, "The compute pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before dispatching");
    if (groupsX > 0 && groupsY > 0 && groupsZ > 0) {
        m_impl->Dispatch(groupsX, groupsY, groupsZ);
    }
}

void ComputePass::DispatchIndirect(const Buffer& buffer, std::uint32_t offset) {
    SPOT_ASSERT(m_impl, "The compute pass has ended");
    SPOT_ASSERT(m_pipelineBound, "Bind a pipeline before dispatching");
    SPOT_ASSERT(buffer.GetUsage() == BufferUsage::Indirect, "Indirect dispatches read indirect buffers");
    SPOT_ASSERT(offset + sizeof(DispatchIndirectArguments) <= buffer.GetSize(),
                "The dispatch's arguments overflow the indirect buffer");
    m_impl->DispatchIndirect(buffer, offset);
}

void* ComputePass::GetNativeHandle() const {
    return m_impl ? m_impl->GetNativeHandle() : nullptr;
}

ComputePipeline::ComputePipeline(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

TextureTable::TextureTable(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}
TextureTable::~TextureTable() = default;
std::uint32_t TextureTable::GetCapacity() const {
    return m_impl->capacity;
}
void* TextureTable::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}
ComputePipeline::~ComputePipeline() = default;
void* ComputePipeline::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}

Buffer::Buffer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}
Buffer::~Buffer() = default;
BufferUsage Buffer::GetUsage() const {
    return m_impl->usage;
}
std::uint32_t Buffer::GetSize() const {
    return m_impl->size;
}
void* Buffer::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}

Shader::Shader(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}
Shader::~Shader() = default;
ShaderStage Shader::GetStage() const {
    return m_impl->stage;
}
void* Shader::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}

Pipeline::Pipeline(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}
Pipeline::~Pipeline() = default;
void* Pipeline::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}

Texture::Texture(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}
Texture::~Texture() = default;
std::uint32_t Texture::GetWidth() const {
    return m_impl->desc.width;
}
std::uint32_t Texture::GetHeight() const {
    return m_impl->desc.height;
}
TextureFormat Texture::GetFormat() const {
    return m_impl->desc.format;
}
TextureType Texture::GetType() const {
    return m_impl->desc.type;
}
std::uint32_t Texture::GetLayerCount() const {
    return m_impl->desc.type == TextureType::Cube ? 6 : 1;
}
bool Texture::IsRenderTarget() const {
    return m_impl->desc.renderTarget;
}
bool Texture::IsStorage() const {
    return m_impl->desc.storage;
}
std::uint32_t Texture::GetMipLevelCount() const {
    return m_impl->mipLevels;
}
std::uint32_t Texture::GetSampleCount() const {
    return m_impl->desc.sampleCount;
}
void* Texture::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}

Sampler::Sampler(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}
Sampler::~Sampler() = default;
void* Sampler::GetNativeHandle() const {
    return m_impl->GetNativeHandle();
}

} // namespace Spot
