#include "graphics/nvrhi/nvrhi_internal.h"

#include "core/assert.h"
#include "core/log.h"

#include "fullscreen.vert.h"
#include "mip_downsample.frag.h"
#include "mip_downsample_layer.frag.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace Spot::Detail {

namespace {

// Frames a binding set or framebuffer is kept after its last use: past every frame in flight that may still use it.
constexpr std::uint64_t CacheFrames = 4;

std::uint32_t GetLevelSize(std::uint32_t size, std::uint32_t mipLevel) {
    return std::max(size >> mipLevel, 1u);
}

bool SameAttachment(const nvrhi::FramebufferAttachment& a, const nvrhi::FramebufferAttachment& b) {
    return a.texture == b.texture && a.subresources == b.subresources;
}

bool SameFramebuffer(const nvrhi::FramebufferDesc& a, const nvrhi::FramebufferDesc& b) {
    if (a.colorAttachments.size() != b.colorAttachments.size() ||
        !SameAttachment(a.depthAttachment, b.depthAttachment)) {
        return false;
    }
    for (std::size_t i = 0; i < a.colorAttachments.size(); ++i) {
        if (!SameAttachment(a.colorAttachments[i], b.colorAttachments[i])) {
            return false;
        }
    }
    return true;
}

// NVRHI has no render pass objects to begin: it records draws against a framebuffer, beginning and ending Vulkan's
// passes itself. A pass here clears its targets when it begins, gathers the state of its draws, and resolves its
// target when it ends.
class NvrhiPass final : public RenderPass::Impl {
public:
    NvrhiPass(NvrhiContext& context, const RenderPassDesc& desc, nvrhi::IFramebuffer* framebuffer, std::uint32_t width,
              std::uint32_t height)
        : m_context(context), m_desc(desc), m_framebuffer(framebuffer) {
        m_viewport = nvrhi::Viewport(static_cast<float>(width), static_cast<float>(height));
        m_scissor = nvrhi::Rect(static_cast<int>(width), static_cast<int>(height));
    }

    void End() override {
        if (m_desc.resolveTarget || m_desc.resolveToWindow) {
            nvrhi::ITexture* destination =
                m_desc.resolveTarget ? GetNvrhiTexture(*m_desc.resolveTarget) : m_context.target;
            m_context.commandList->resolveTexture(destination, nvrhi::AllSubresources, GetNvrhiTexture(*m_desc.target),
                                                  nvrhi::AllSubresources);
        }
        for (const ColorTarget& color : m_desc.additionalTargets) {
            if (color.resolveTarget) {
                m_context.commandList->resolveTexture(GetNvrhiTexture(*color.resolveTarget), nvrhi::AllSubresources,
                                                      GetNvrhiTexture(*color.texture), nvrhi::AllSubresources);
            }
        }
        m_context.passOpen = false;
    }

    void BindPipeline(const Pipeline& pipeline) override {
        m_pipeline = &static_cast<const NvrhiPipeline&>(GraphicsAccess::GetImpl(pipeline));
    }

    void SetViewport(const Viewport& viewport) override {
        m_viewport = nvrhi::Viewport(viewport.x, viewport.x + viewport.width, viewport.y, viewport.y + viewport.height,
                                     viewport.minDepth, viewport.maxDepth);
    }

    void SetScissor(const ScissorRect& rect) override {
        m_scissor = nvrhi::Rect(rect.x, rect.x + rect.width, rect.y, rect.y + rect.height);
    }

    void BindVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint32_t offset) override {
        if (slot >= m_vertexBuffers.size()) {
            m_vertexBuffers.resize(slot + 1);
        }
        m_vertexBuffers[slot] = {GetNvrhiBuffer(buffer), slot, offset};
    }

    void BindIndexBuffer(const Buffer& buffer, IndexType type, std::uint32_t offset) override {
        m_indexBuffer = {GetNvrhiBuffer(buffer),
                         type == IndexType::UInt16 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT, offset};
    }

    void BindFragmentTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) override {
        SPOT_ASSERT(slot < MaxStageTextures, "Fragment texture slot {} is past the last one", slot);
        m_context.stages[1].textures[slot] = GetNvrhiTexture(texture);
        m_context.stages[1].samplers[slot] = GetNvrhiSampler(sampler);
    }

    void BindVertexStorageBuffer(std::uint32_t slot, const Buffer& buffer) override {
        SPOT_ASSERT(slot < MaxStageStorageBuffers, "Storage buffer slot {} is past the last one", slot);
        m_context.stages[0].storageBuffers[slot] = GetNvrhiBuffer(buffer);
    }

    void BindFragmentStorageBuffer(std::uint32_t slot, const Buffer& buffer) override {
        SPOT_ASSERT(slot < MaxStageStorageBuffers, "Storage buffer slot {} is past the last one", slot);
        m_context.stages[1].storageBuffers[slot] = GetNvrhiBuffer(buffer);
    }

    void PushVertexUniformData(std::uint32_t slot, const void* data, std::uint32_t size) override {
        m_context.PushUniforms(0, slot, data, size);
    }

    void PushFragmentUniformData(std::uint32_t slot, const void* data, std::uint32_t size) override {
        m_context.PushUniforms(1, slot, data, size);
    }

    void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
              std::uint32_t firstInstance) override {
        if (!SetState(false)) {
            return;
        }
        nvrhi::DrawArguments arguments;
        arguments.vertexCount = vertexCount;
        arguments.instanceCount = instanceCount;
        arguments.startVertexLocation = firstVertex;
        arguments.startInstanceLocation = firstInstance;
        m_context.commandList->draw(arguments);
    }

    void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                     std::int32_t vertexOffset, std::uint32_t firstInstance) override {
        if (!SetState(true)) {
            return;
        }
        nvrhi::DrawArguments arguments;
        arguments.vertexCount = indexCount;
        arguments.instanceCount = instanceCount;
        arguments.startIndexLocation = firstIndex;
        arguments.startVertexLocation = static_cast<std::uint32_t>(vertexOffset);
        arguments.startInstanceLocation = firstInstance;
        m_context.commandList->drawIndexed(arguments);
    }

    void DrawIndirect(const Buffer& buffer, std::uint32_t offset, std::uint32_t drawCount) override {
        if (SetState(false, GetNvrhiBuffer(buffer))) {
            m_context.commandList->drawIndirect(offset, drawCount);
        }
    }

    void DrawIndexedIndirect(const Buffer& buffer, std::uint32_t offset, std::uint32_t drawCount) override {
        if (SetState(true, GetNvrhiBuffer(buffer))) {
            m_context.commandList->drawIndexedIndirect(offset, drawCount);
        }
    }

    void* GetNativeHandle() const override { return m_context.commandList.Get(); }

private:
    // The binding set of a descriptor set of the pipeline, from what the stage has bound.
    nvrhi::IBindingSet* MakeBindings(DescriptorSet set) {
        const bool vertex = set == DescriptorSet::VertexResources || set == DescriptorSet::VertexUniforms;
        const StageResources& resources = vertex ? m_pipeline->vertex : m_pipeline->fragment;
        const NvrhiContext::StageBindings& bound = m_context.stages[vertex ? 0 : 1];
        nvrhi::BindingSetDesc desc;
        if (set == DescriptorSet::VertexUniforms || set == DescriptorSet::FragmentUniforms) {
            for (std::uint32_t slot = 0; slot < resources.uniformBuffers; ++slot) {
                if (!bound.uniforms[slot]) {
                    Log::GetEngineLogger().Error("A draw reads uniforms at slot {} that were never pushed", slot);
                    return nullptr;
                }
                desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(slot, bound.uniforms[slot]));
            }
        } else {
            for (std::uint32_t slot = 0; slot < resources.textures; ++slot) {
                if (!bound.textures[slot] || !bound.samplers[slot]) {
                    Log::GetEngineLogger().Error("A draw samples texture slot {}, which nothing is bound to", slot);
                    return nullptr;
                }
                desc.addItem(nvrhi::BindingSetItem::Texture_SRV(slot, bound.textures[slot]));
                desc.addItem(nvrhi::BindingSetItem::Sampler(slot, bound.samplers[slot]));
            }
            for (std::uint32_t slot = 0; slot < resources.storageBuffers; ++slot) {
                if (!bound.storageBuffers[slot]) {
                    Log::GetEngineLogger().Error("A draw reads storage buffer slot {}, which nothing is bound to",
                                                 slot);
                    return nullptr;
                }
                desc.addItem(
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(resources.textures + slot, bound.storageBuffers[slot]));
            }
        }
        return m_context.GetBindingSet(m_pipeline->layouts[static_cast<std::size_t>(set)], desc);
    }

    bool SetState(bool indexed, nvrhi::IBuffer* indirect = nullptr) {
        SPOT_ASSERT(m_pipeline, "Bind a pipeline before drawing");
        if (!m_framebuffer) {
            return false;
        }
        nvrhi::GraphicsState state;
        state.pipeline = m_pipeline->handle;
        state.framebuffer = m_framebuffer;
        state.viewport.addViewport(m_viewport);
        state.viewport.addScissorRect(m_scissor);
        for (std::size_t set = 0; set < DescriptorSetCount; ++set) {
            nvrhi::IBindingSet* bindings = MakeBindings(static_cast<DescriptorSet>(set));
            if (!bindings) {
                return false;
            }
            state.addBindingSet(bindings);
        }
        for (const nvrhi::VertexBufferBinding& binding : m_vertexBuffers) {
            if (binding.buffer) {
                state.addVertexBuffer(binding);
            }
        }
        if (indexed) {
            state.indexBuffer = m_indexBuffer;
        }
        state.indirectParams = indirect;
        m_context.commandList->setGraphicsState(state);
        return true;
    }

    NvrhiContext& m_context;
    RenderPassDesc m_desc;
    nvrhi::IFramebuffer* m_framebuffer;
    const NvrhiPipeline* m_pipeline = nullptr;
    nvrhi::Viewport m_viewport;
    nvrhi::Rect m_scissor;
    std::vector<nvrhi::VertexBufferBinding> m_vertexBuffers;
    nvrhi::IndexBufferBinding m_indexBuffer{nullptr, nvrhi::Format::R32_UINT, 0};
};

// A compute pass: it gathers what dispatches read, like render passes do for draws, and binds what the pass writes,
// as it was named when the pass began.
class NvrhiComputePass final : public ComputePass::Impl {
public:
    NvrhiComputePass(NvrhiContext& context, const ComputePassDesc& desc) : m_context(context), m_desc(desc) {}

    void End() override { m_context.passOpen = false; }

    void BindPipeline(const ComputePipeline& pipeline) override {
        m_pipeline = &static_cast<const NvrhiComputePipeline&>(GraphicsAccess::GetImpl(pipeline));
    }

    void BindTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler) override {
        SPOT_ASSERT(slot < MaxStageTextures, "Texture slot {} is past the last one", slot);
        Bindings().textures[slot] = GetNvrhiTexture(texture);
        Bindings().samplers[slot] = GetNvrhiSampler(sampler);
    }

    void BindStorageBuffer(std::uint32_t slot, const Buffer& buffer) override {
        SPOT_ASSERT(slot < MaxStageStorageBuffers, "Storage buffer slot {} is past the last one", slot);
        Bindings().storageBuffers[slot] = GetNvrhiBuffer(buffer);
    }

    void PushUniformData(std::uint32_t slot, const void* data, std::uint32_t size) override {
        m_context.PushUniforms(NvrhiContext::ComputeStage, slot, data, size);
    }

    void Dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) override {
        if (SetState(nullptr)) {
            m_context.commandList->dispatch(groupsX, groupsY, groupsZ);
        }
    }

    void DispatchIndirect(const Buffer& buffer, std::uint32_t offset) override {
        if (SetState(GetNvrhiBuffer(buffer))) {
            m_context.commandList->dispatchIndirect(offset);
        }
    }

    void* GetNativeHandle() const override { return m_context.commandList.Get(); }

private:
    NvrhiContext::StageBindings& Bindings() { return m_context.stages[NvrhiContext::ComputeStage]; }

    nvrhi::IBindingSet* MakeBindings(ComputeSet set) {
        const StageResources& resources = m_pipeline->resources;
        const NvrhiContext::StageBindings& bound = Bindings();
        nvrhi::BindingSetDesc desc;
        switch (set) {
        case ComputeSet::Resources:
            for (std::uint32_t slot = 0; slot < resources.textures; ++slot) {
                if (!bound.textures[slot] || !bound.samplers[slot]) {
                    Log::GetEngineLogger().Error("A dispatch samples texture slot {}, which nothing is bound to", slot);
                    return nullptr;
                }
                desc.addItem(nvrhi::BindingSetItem::Texture_SRV(slot, bound.textures[slot]));
                desc.addItem(nvrhi::BindingSetItem::Sampler(slot, bound.samplers[slot]));
            }
            for (std::uint32_t slot = 0; slot < resources.storageBuffers; ++slot) {
                if (!bound.storageBuffers[slot]) {
                    Log::GetEngineLogger().Error("A dispatch reads storage buffer slot {}, which nothing is bound to",
                                                 slot);
                    return nullptr;
                }
                desc.addItem(
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(resources.textures + slot, bound.storageBuffers[slot]));
            }
            break;
        case ComputeSet::Writes:
            if (resources.readWriteTextures > m_desc.textures.size() ||
                resources.readWriteBuffers > m_desc.buffers.size()) {
                Log::GetEngineLogger().Error("A dispatch writes more than its compute pass was given to write");
                return nullptr;
            }
            for (std::uint32_t slot = 0; slot < resources.readWriteTextures; ++slot) {
                const StorageTexture& storage = m_desc.textures[slot];
                desc.addItem(nvrhi::BindingSetItem::Texture_UAV(
                    slot, GetNvrhiTexture(*storage.texture), nvrhi::Format::UNKNOWN,
                    nvrhi::TextureSubresourceSet(storage.mipLevel, 1, 0, 1), nvrhi::TextureDimension::Texture2D));
            }
            for (std::uint32_t slot = 0; slot < resources.readWriteBuffers; ++slot) {
                desc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(resources.readWriteTextures + slot,
                                                                         GetNvrhiBuffer(*m_desc.buffers[slot])));
            }
            break;
        default:
            for (std::uint32_t slot = 0; slot < resources.uniformBuffers; ++slot) {
                if (!bound.uniforms[slot]) {
                    Log::GetEngineLogger().Error("A dispatch reads uniforms at slot {} that were never pushed", slot);
                    return nullptr;
                }
                desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(slot, bound.uniforms[slot]));
            }
            break;
        }
        return m_context.GetBindingSet(m_pipeline->layouts[static_cast<std::size_t>(set)], desc);
    }

    bool SetState(nvrhi::IBuffer* indirect) {
        SPOT_ASSERT(m_pipeline, "Bind a pipeline before dispatching");
        nvrhi::ComputeState state;
        state.pipeline = m_pipeline->handle;
        for (std::size_t set = 0; set < ComputeSetCount; ++set) {
            nvrhi::IBindingSet* bindings = MakeBindings(static_cast<ComputeSet>(set));
            if (!bindings) {
                return false;
            }
            state.addBindingSet(bindings);
        }
        state.indirectParams = indirect;
        m_context.commandList->setComputeState(state);
        return true;
    }

    NvrhiContext& m_context;
    ComputePassDesc m_desc;
    const NvrhiComputePipeline* m_pipeline = nullptr;
};

} // namespace

std::unique_ptr<ComputePass::Impl> NvrhiContext::BeginComputePass(const ComputePassDesc& desc) {
    SPOT_ASSERT(!passOpen, "Only one pass can be open at a time");
    SPOT_ASSERT(frameOpen, "Compute passes need an open frame");
    passOpen = true;
    return std::make_unique<NvrhiComputePass>(*this, desc);
}

std::size_t NvrhiContext::BindingSetKeyHash::operator()(const BindingSetKey& key) const {
    std::size_t value = std::hash<nvrhi::BindingSetDesc>()(key.desc);
    nvrhi::hash_combine(value, key.layout);
    return value;
}

void NvrhiContext::PushUniforms(std::size_t stage, std::uint32_t slot, const void* data, std::uint32_t size) {
    SPOT_ASSERT(slot < MaxStageUniforms, "Uniform slot {} is past the last one", slot);
    nvrhi::BufferHandle& buffer = stages[stage].uniforms[slot];
    if (!buffer || buffer->getDesc().byteSize < size) {
        // Sized to a power of two, so uniforms that grow seldom need a new buffer; a version of it is taken by each
        // push, from a ring of about two megabytes.
        const std::uint64_t byteSize = std::max<std::uint64_t>(std::bit_ceil(static_cast<std::uint64_t>(size)), 256);
        nvrhi::BufferDesc desc;
        desc.byteSize = byteSize;
        desc.isConstantBuffer = true;
        desc.isVolatile = true;
        desc.maxVersions = static_cast<std::uint32_t>(std::clamp<std::uint64_t>((2u << 20) / byteSize, 64, 16384));
        desc.debugName = stage == 0 ? "Vertex uniforms" : stage == 1 ? "Fragment uniforms" : "Compute uniforms";
        buffer = device->createBuffer(desc);
    }
    commandList->writeBuffer(buffer, data, size);
}

nvrhi::IBindingSet* NvrhiContext::GetBindingSet(nvrhi::IBindingLayout* layout, const nvrhi::BindingSetDesc& desc) {
    BindingSetKey key{layout, desc};
    auto found = m_bindingSets.find(key);
    if (found == m_bindingSets.end()) {
        nvrhi::BindingSetHandle handle = device->createBindingSet(desc, layout);
        if (!handle) {
            Log::GetEngineLogger().Error("Failed to create a binding set");
            return nullptr;
        }
        found = m_bindingSets.emplace(std::move(key), CachedBindingSet{std::move(handle), m_frame}).first;
    }
    found->second.lastUsed = m_frame;
    return found->second.handle;
}

nvrhi::IFramebuffer* NvrhiContext::GetFramebuffer(const nvrhi::FramebufferDesc& desc) {
    for (auto& [cachedDesc, cached] : m_framebuffers) {
        if (SameFramebuffer(cachedDesc, desc)) {
            cached.lastUsed = m_frame;
            return cached.handle;
        }
    }
    nvrhi::FramebufferHandle handle = device->createFramebuffer(desc);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create a framebuffer");
        return nullptr;
    }
    m_framebuffers.emplace_back(desc, CachedFramebuffer{handle, m_frame});
    return handle;
}

void NvrhiContext::BeginGpuTimer(std::string_view name) {
    SPOT_ASSERT(frameOpen && !passOpen, "GPU timers begin in a frame, between passes");
    FrameTimers& timers = m_timers[frameSlot];
    if (timers.used == timers.queries.size()) {
        timers.queries.push_back(device->createTimerQuery());
        timers.timings.emplace_back();
    }
    const std::size_t index = timers.used++;
    timers.timings[index] = {std::string(name), 0.0, static_cast<std::uint32_t>(timers.open.size())};
    timers.open.push_back(index);
    commandList->beginTimerQuery(timers.queries[index]);
}

void NvrhiContext::EndGpuTimer() {
    SPOT_ASSERT(frameOpen && !passOpen, "GPU timers end in a frame, between passes");
    FrameTimers& timers = m_timers[frameSlot];
    SPOT_ASSERT(!timers.open.empty(), "EndGpuTimer() without a timer begun");
    commandList->endTimerQuery(timers.queries[timers.open.back()]);
    timers.open.pop_back();
}

void NvrhiContext::CollectTimers(std::size_t slot, std::vector<GpuTiming>& timings) {
    FrameTimers& timers = m_timers[slot];
    if (timers.used == 0) {
        return;
    }
    timings.clear();
    for (std::size_t i = 0; i < timers.used; ++i) {
        GpuTiming timing = timers.timings[i];
        timing.milliseconds = device->getTimerQueryTime(timers.queries[i]) * 1000.0;
        device->resetTimerQuery(timers.queries[i]);
        timings.push_back(std::move(timing));
    }
    timers.used = 0;
}

void NvrhiContext::FinishFrame() {
    std::erase_if(m_bindingSets, [this](const auto& entry) { return entry.second.lastUsed + CacheFrames < m_frame; });
    std::erase_if(m_framebuffers, [this](const auto& entry) { return entry.second.lastUsed + CacheFrames < m_frame; });
    // Pushed uniforms only last for the frame they were written in.
    for (StageBindings& stage : stages) {
        stage = {.uniforms = stage.uniforms};
    }
    ++m_frame;
}

void NvrhiContext::Release() {
    m_bindingSets.clear();
    m_framebuffers.clear();
    m_mipPipelines.clear();
    m_timers = {};
    m_mipLayouts = {};
    m_mipSampler = nullptr;
    m_mipVertexShader = nullptr;
    m_mipFragmentShaders = {};
    stages = {};
    commandList = nullptr;
}

std::unique_ptr<RenderPass::Impl> NvrhiContext::BeginRenderPass(const RenderPassDesc& desc) {
    SPOT_ASSERT(!passOpen, "Only one render pass can be open at a time");
    SPOT_ASSERT(frameOpen, "Render passes need an open frame");
    nvrhi::FramebufferDesc framebuffer;
    std::uint32_t passWidth = width;
    std::uint32_t passHeight = height;
    if (!desc.depthOnly) {
        nvrhi::ITexture* color = desc.target ? GetNvrhiTexture(*desc.target) : target;
        const nvrhi::TextureSubresourceSet subresources(desc.targetMipLevel, 1, desc.targetLayer, 1);
        if (desc.clearColor) {
            const Color& clear = *desc.clearColor;
            commandList->clearTextureFloat(color, subresources, nvrhi::Color(clear.r, clear.g, clear.b, clear.a));
        }
        framebuffer.addColorAttachment(color, subresources);
        for (const ColorTarget& additional : desc.additionalTargets) {
            nvrhi::ITexture* texture = GetNvrhiTexture(*additional.texture);
            const nvrhi::TextureSubresourceSet levels(additional.mipLevel, 1, additional.layer, 1);
            if (additional.clearColor) {
                const Color& clear = *additional.clearColor;
                commandList->clearTextureFloat(texture, levels, nvrhi::Color(clear.r, clear.g, clear.b, clear.a));
            }
            framebuffer.addColorAttachment(texture, levels);
        }
        if (desc.target) {
            passWidth = GetLevelSize(desc.target->GetWidth(), desc.targetMipLevel);
            passHeight = GetLevelSize(desc.target->GetHeight(), desc.targetMipLevel);
        }
    }
    if (desc.depthTarget) {
        nvrhi::ITexture* depth = GetNvrhiTexture(*desc.depthTarget);
        if (desc.clearDepth) {
            commandList->clearDepthStencilTexture(depth, nvrhi::AllSubresources, true, *desc.clearDepth, false, 0);
        }
        framebuffer.setDepthAttachment(depth);
        if (desc.depthOnly) {
            passWidth = desc.depthTarget->GetWidth();
            passHeight = desc.depthTarget->GetHeight();
        }
    }
    nvrhi::IFramebuffer* native = GetFramebuffer(framebuffer);
    passOpen = true;
    return std::make_unique<NvrhiPass>(*this, desc, native, passWidth, passHeight);
}

bool NvrhiContext::SetFrameTarget(Texture* frameTarget) {
    if (passOpen || !frameOpen || !acquiredTarget) {
        return false;
    }
    if (frameTarget && (!frameTarget->IsRenderTarget() || frameTarget->GetSampleCount() != 1 ||
                        frameTarget->GetLayerCount() != 1 || frameTarget->GetFormat() != format ||
                        frameTarget->GetWidth() != width || frameTarget->GetHeight() != height)) {
        Log::GetEngineLogger().Warning("Frame targets must match the acquired frame's size and format");
        return false;
    }
    target = frameTarget ? GetNvrhiTexture(*frameTarget) : acquiredTarget;
    return true;
}

bool NvrhiContext::BlitTextureToFrame(const Texture& source) {
    nvrhi::ITexture* texture = GetNvrhiTexture(source);
    if (passOpen || !frameOpen || !target || texture == target || source.GetSampleCount() != 1 ||
        source.GetLayerCount() != 1 || source.GetFormat() != format || source.GetWidth() != width ||
        source.GetHeight() != height) {
        return false;
    }
    commandList->copyTexture(target, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
    return true;
}

void NvrhiContext::GenerateMipmaps(Texture& texture) {
    SPOT_ASSERT(!passOpen, "Mipmaps cannot be generated during a render pass");
    SPOT_ASSERT(frameOpen, "Mipmaps are generated in an open frame");
    RecordMipmaps(*commandList, texture);
}

nvrhi::IGraphicsPipeline* NvrhiContext::GetMipmapPipeline(nvrhi::Format targetFormat, bool layers) {
    const std::uint32_t key = static_cast<std::uint32_t>(targetFormat) * 2 + (layers ? 1 : 0);
    if (const auto found = m_mipPipelines.find(key); found != m_mipPipelines.end()) {
        return found->second;
    }
    if (!m_mipVertexShader) {
        nvrhi::ShaderDesc vertex;
        vertex.shaderType = nvrhi::ShaderType::Vertex;
        m_mipVertexShader = device->createShader(vertex, Shaders::FullscreenVert.vulkanSpirv.data(),
                                                 Shaders::FullscreenVert.vulkanSpirv.size());
        nvrhi::ShaderDesc fragment;
        fragment.shaderType = nvrhi::ShaderType::Pixel;
        m_mipFragmentShaders[0] = device->createShader(fragment, Shaders::MipDownsampleFrag.vulkanSpirv.data(),
                                                       Shaders::MipDownsampleFrag.vulkanSpirv.size());
        m_mipFragmentShaders[1] = device->createShader(fragment, Shaders::MipDownsampleLayerFrag.vulkanSpirv.data(),
                                                       Shaders::MipDownsampleLayerFrag.vulkanSpirv.size());
        for (std::uint32_t set = 0; set < m_mipLayouts.size(); ++set) {
            nvrhi::BindingLayoutDesc layout;
            layout.visibility = set == 2 ? nvrhi::ShaderType::Pixel : nvrhi::ShaderType::Vertex;
            layout.registerSpace = set;
            layout.registerSpaceIsDescriptorSet = true;
            if (set == 2) {
                layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
                layout.addItem(nvrhi::BindingLayoutItem::Sampler(0));
            }
            m_mipLayouts[set] = device->createBindingLayout(layout);
        }
        m_mipSampler = device->createSampler(
            nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge));
    }
    nvrhi::GraphicsPipelineDesc desc;
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    desc.VS = m_mipVertexShader;
    desc.PS = m_mipFragmentShaders[layers ? 1 : 0];
    for (const auto& layout : m_mipLayouts) {
        desc.addBindingLayout(layout);
    }
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    desc.renderState.depthStencilState.depthTestEnable = false;
    desc.renderState.depthStencilState.depthWriteEnable = false;
    nvrhi::FramebufferInfo framebuffer;
    framebuffer.addColorFormat(targetFormat);
    nvrhi::GraphicsPipelineHandle pipeline = device->createGraphicsPipeline(desc, framebuffer);
    if (!pipeline) {
        Log::GetEngineLogger().Error("Failed to create the pipeline that generates mipmaps");
        return nullptr;
    }
    return m_mipPipelines.emplace(key, std::move(pipeline)).first->second;
}

void NvrhiContext::RecordMipmaps(nvrhi::ICommandList& commands, Texture& texture) {
    const std::uint32_t levels = texture.GetMipLevelCount();
    if (levels <= 1) {
        return;
    }
    nvrhi::ITexture* native = GetNvrhiTexture(texture);
    // Faces of a cube are read through array views, as NVRHI views no face of a cube as a 2D texture.
    const bool layers = texture.GetLayerCount() > 1;
    nvrhi::IGraphicsPipeline* pipeline = GetMipmapPipeline(native->getDesc().format, layers);
    if (!pipeline) {
        return;
    }
    nvrhi::IBindingSet* empty[2] = {GetBindingSet(m_mipLayouts[0], {}), GetBindingSet(m_mipLayouts[1], {})};
    for (std::uint32_t layer = 0; layer < texture.GetLayerCount(); ++layer) {
        for (std::uint32_t level = 1; level < levels; ++level) {
            // A view of the face's level above, which the draw reads while it writes the level below.
            nvrhi::BindingSetDesc bindings;
            bindings.addItem(nvrhi::BindingSetItem::Texture_SRV(
                0, native, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(level - 1, 1, layer, 1),
                layers ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D));
            bindings.addItem(nvrhi::BindingSetItem::Sampler(0, m_mipSampler));
            nvrhi::FramebufferDesc framebufferDesc;
            framebufferDesc.addColorAttachment(native, nvrhi::TextureSubresourceSet(level, 1, layer, 1));
            nvrhi::IFramebuffer* framebuffer = GetFramebuffer(framebufferDesc);
            nvrhi::IBindingSet* source = GetBindingSet(m_mipLayouts[2], bindings);
            if (!framebuffer || !source || !empty[0] || !empty[1]) {
                return;
            }
            const auto levelWidth = static_cast<float>(GetLevelSize(texture.GetWidth(), level));
            const auto levelHeight = static_cast<float>(GetLevelSize(texture.GetHeight(), level));
            nvrhi::GraphicsState state;
            state.pipeline = pipeline;
            state.framebuffer = framebuffer;
            state.viewport.addViewportAndScissorRect(nvrhi::Viewport(levelWidth, levelHeight));
            state.addBindingSet(empty[0]).addBindingSet(empty[1]).addBindingSet(source);
            commands.setGraphicsState(state);
            commands.draw(nvrhi::DrawArguments().setVertexCount(3));
        }
    }
}

} // namespace Spot::Detail
