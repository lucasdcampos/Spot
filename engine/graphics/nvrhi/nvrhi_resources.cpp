#include "graphics/nvrhi/nvrhi_internal.h"

#include "core/log.h"

#include <algorithm>

namespace Spot::Detail {

namespace {

nvrhi::Format ToNvrhi(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float:
        return nvrhi::Format::R32_FLOAT;
    case VertexFormat::Float2:
        return nvrhi::Format::RG32_FLOAT;
    case VertexFormat::Float3:
        return nvrhi::Format::RGB32_FLOAT;
    case VertexFormat::Float4:
        return nvrhi::Format::RGBA32_FLOAT;
    case VertexFormat::UInt4:
        return nvrhi::Format::RGBA32_UINT;
    case VertexFormat::UByte4Normalized:
        return nvrhi::Format::RGBA8_UNORM;
    }
    return nvrhi::Format::UNKNOWN;
}

nvrhi::PrimitiveType ToNvrhi(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::TriangleList:
        return nvrhi::PrimitiveType::TriangleList;
    case PrimitiveType::TriangleStrip:
        return nvrhi::PrimitiveType::TriangleStrip;
    case PrimitiveType::LineList:
        return nvrhi::PrimitiveType::LineList;
    case PrimitiveType::LineStrip:
        return nvrhi::PrimitiveType::LineStrip;
    case PrimitiveType::PointList:
        return nvrhi::PrimitiveType::PointList;
    }
    return nvrhi::PrimitiveType::TriangleList;
}

nvrhi::ComparisonFunc ToNvrhi(CompareOp op) {
    switch (op) {
    case CompareOp::Never:
        return nvrhi::ComparisonFunc::Never;
    case CompareOp::Less:
        return nvrhi::ComparisonFunc::Less;
    case CompareOp::LessOrEqual:
        return nvrhi::ComparisonFunc::LessOrEqual;
    case CompareOp::Equal:
        return nvrhi::ComparisonFunc::Equal;
    case CompareOp::Greater:
        return nvrhi::ComparisonFunc::Greater;
    case CompareOp::GreaterOrEqual:
        return nvrhi::ComparisonFunc::GreaterOrEqual;
    case CompareOp::NotEqual:
        return nvrhi::ComparisonFunc::NotEqual;
    case CompareOp::Always:
        return nvrhi::ComparisonFunc::Always;
    }
    return nvrhi::ComparisonFunc::Always;
}

nvrhi::BlendState::RenderTarget ToNvrhi(BlendMode mode) {
    nvrhi::BlendState::RenderTarget blend;
    if (mode == BlendMode::None) {
        return blend;
    }
    blend.blendEnable = true;
    blend.srcBlend = mode == BlendMode::Premultiplied ? nvrhi::BlendFactor::One : nvrhi::BlendFactor::SrcAlpha;
    blend.srcBlendAlpha = nvrhi::BlendFactor::One;
    if (mode == BlendMode::Alpha || mode == BlendMode::Premultiplied) {
        blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
        blend.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
    } else {
        blend.destBlend = nvrhi::BlendFactor::One;
        blend.destBlendAlpha = nvrhi::BlendFactor::One;
    }
    return blend;
}

nvrhi::SamplerAddressMode ToNvrhi(TextureAddressMode mode) {
    switch (mode) {
    case TextureAddressMode::ClampToEdge:
        return nvrhi::SamplerAddressMode::ClampToEdge;
    case TextureAddressMode::Repeat:
        return nvrhi::SamplerAddressMode::Repeat;
    case TextureAddressMode::MirroredRepeat:
        return nvrhi::SamplerAddressMode::MirroredRepeat;
    }
    return nvrhi::SamplerAddressMode::ClampToEdge;
}

// The layout of one descriptor set: a stage's texture and sampler pairs and the storage buffers after them, or its
// uniforms.
nvrhi::BindingLayoutHandle CreateLayout(nvrhi::IDevice& device, DescriptorSet set, const StageResources& resources) {
    const bool vertex = set == DescriptorSet::VertexResources || set == DescriptorSet::VertexUniforms;
    nvrhi::BindingLayoutDesc desc;
    desc.visibility = vertex ? nvrhi::ShaderType::Vertex : nvrhi::ShaderType::Pixel;
    desc.registerSpace = static_cast<std::uint32_t>(set);
    desc.registerSpaceIsDescriptorSet = true;
    if (set == DescriptorSet::VertexUniforms || set == DescriptorSet::FragmentUniforms) {
        for (std::uint32_t slot = 0; slot < resources.uniformBuffers; ++slot) {
            desc.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(slot));
        }
    } else {
        for (std::uint32_t slot = 0; slot < resources.textures; ++slot) {
            desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(slot));
            desc.addItem(nvrhi::BindingLayoutItem::Sampler(slot));
        }
        for (std::uint32_t slot = 0; slot < resources.storageBuffers; ++slot) {
            desc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(resources.textures + slot));
        }
    }
    return device.createBindingLayout(desc);
}

} // namespace

std::unique_ptr<Shader> NvrhiDevice::CreateShader(const ShaderDesc& desc) {
    if (desc.vulkanSpirv.empty()) {
        Log::GetEngineLogger().Error("Shader has no SPIR-V for the Vulkan backend; build it with spot_add_shaders()");
        return nullptr;
    }
    if (desc.storageTextureCount > 0) {
        Log::GetEngineLogger().Error("Shaders read storage textures through samplers on the Vulkan backend");
        return nullptr;
    }
    if (desc.samplerCount > MaxStageTextures || desc.storageBufferCount > MaxStageStorageBuffers ||
        desc.uniformBufferCount > MaxStageUniforms || desc.readWriteStorageTextureCount > MaxStageTextures ||
        desc.readWriteStorageBufferCount > MaxStageStorageBuffers) {
        Log::GetEngineLogger().Error("Shader reads more resources than a stage can bind");
        return nullptr;
    }
    nvrhi::ShaderDesc native;
    native.shaderType = desc.stage == ShaderStage::Vertex     ? nvrhi::ShaderType::Vertex
                        : desc.stage == ShaderStage::Fragment ? nvrhi::ShaderType::Pixel
                                                              : nvrhi::ShaderType::Compute;
    native.entryName = "main";
    nvrhi::ShaderHandle handle = m_device->createShader(native, desc.vulkanSpirv.data(), desc.vulkanSpirv.size());
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create shader");
        return nullptr;
    }
    auto impl = std::make_unique<NvrhiShader>();
    impl->shared = m_shared;
    impl->handle = std::move(handle);
    impl->stage = desc.stage;
    impl->resources = {desc.samplerCount, desc.storageBufferCount, desc.uniformBufferCount,
                       desc.readWriteStorageTextureCount, desc.readWriteStorageBufferCount};
    return GraphicsAccess::Make<Shader>(std::move(impl));
}

std::unique_ptr<Pipeline> NvrhiDevice::CreatePipeline(const PipelineDesc& desc) {
    const auto& vertexShader = static_cast<const NvrhiShader&>(GraphicsAccess::GetImpl(*desc.vertexShader));
    const auto& fragmentShader = static_cast<const NvrhiShader&>(GraphicsAccess::GetImpl(*desc.fragmentShader));
    if (desc.sampleCount != 1 && desc.sampleCount != 2 && desc.sampleCount != 4 && desc.sampleCount != 8) {
        Log::GetEngineLogger().Error("Pipelines cannot draw with {} samples, only 1, 2, 4 or 8", desc.sampleCount);
        return nullptr;
    }

    auto impl = std::make_unique<NvrhiPipeline>();
    impl->shared = m_shared;
    impl->vertex = vertexShader.resources;
    impl->fragment = fragmentShader.resources;
    impl->layouts[0] = CreateLayout(*m_device, DescriptorSet::VertexResources, impl->vertex);
    impl->layouts[1] = CreateLayout(*m_device, DescriptorSet::VertexUniforms, impl->vertex);
    impl->layouts[2] = CreateLayout(*m_device, DescriptorSet::FragmentResources, impl->fragment);
    impl->layouts[3] = CreateLayout(*m_device, DescriptorSet::FragmentUniforms, impl->fragment);

    // Vertex shader inputs take locations in the order of their attributes, so attributes are given by location.
    std::vector<nvrhi::VertexAttributeDesc> attributes;
    std::vector<std::uint32_t> locations;
    for (std::uint32_t slot = 0; slot < desc.vertexBuffers.size(); ++slot) {
        const VertexBufferLayout& layout = desc.vertexBuffers[slot];
        for (const VertexAttribute& attribute : layout.attributes) {
            nvrhi::VertexAttributeDesc native;
            native.name = "ATTRIBUTE" + std::to_string(attribute.location);
            native.format = ToNvrhi(attribute.format);
            native.bufferIndex = slot;
            native.offset = attribute.offset;
            native.elementStride = layout.stride;
            native.isInstanced = layout.inputRate == VertexInputRate::Instance;
            attributes.push_back(native);
            locations.push_back(attribute.location);
        }
    }
    std::vector<std::size_t> order(attributes.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, {}, [&](std::size_t i) { return locations[i]; });
    std::vector<nvrhi::VertexAttributeDesc> sorted;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (locations[order[i]] != i) {
            Log::GetEngineLogger().Error("The Vulkan backend needs vertex attributes at locations 0 to {}",
                                         order.size() - 1);
            return nullptr;
        }
        sorted.push_back(attributes[order[i]]);
    }

    nvrhi::GraphicsPipelineDesc native;
    native.primType = ToNvrhi(desc.primitiveType);
    if (!sorted.empty()) {
        native.inputLayout =
            m_device->createInputLayout(sorted.data(), static_cast<std::uint32_t>(sorted.size()), vertexShader.handle);
    }
    native.VS = vertexShader.handle;
    native.PS = fragmentShader.handle;
    for (const auto& layout : impl->layouts) {
        native.addBindingLayout(layout);
    }
    if (desc.textureTable) {
        native.addBindingLayout(GetTextureTableLayout());
        impl->textureTable = true;
    }
    nvrhi::RenderState& state = native.renderState;
    for (std::size_t target = 0; target <= desc.additionalTargetFormats.size(); ++target) {
        state.blendState.targets[target] = ToNvrhi(desc.blendMode);
    }
    state.rasterState.cullMode = desc.cullMode == CullMode::Back    ? nvrhi::RasterCullMode::Back
                                 : desc.cullMode == CullMode::Front ? nvrhi::RasterCullMode::Front
                                                                    : nvrhi::RasterCullMode::None;
    state.rasterState.frontCounterClockwise = true;
    state.rasterState.scissorEnable = true;
    state.rasterState.depthClipEnable = !(desc.depth && desc.depth->clamp);
    nvrhi::FramebufferInfo framebuffer;
    framebuffer.sampleCount = desc.sampleCount;
    if (!desc.depthOnly) {
        framebuffer.addColorFormat(ToNvrhi(desc.targetFormat.value_or(GetWindowFormat())));
        for (const TextureFormat format : desc.additionalTargetFormats) {
            framebuffer.addColorFormat(ToNvrhi(format));
        }
    }
    if (desc.depth) {
        state.depthStencilState.depthTestEnable = desc.depth->test;
        state.depthStencilState.depthWriteEnable = desc.depth->write;
        state.depthStencilState.depthFunc = ToNvrhi(desc.depth->compare);
        // NVRHI only enables depth bias when its constant factor is not 0, so a slope alone takes the smallest
        // constant there is, a unit of the depth's precision, which is too little to show.
        state.rasterState.depthBias = static_cast<int>(desc.depth->biasConstant);
        if (state.rasterState.depthBias == 0 && desc.depth->biasSlope != 0.0f) {
            state.rasterState.depthBias = desc.depth->biasSlope > 0.0f ? 1 : -1;
        }
        state.rasterState.slopeScaledDepthBias = desc.depth->biasSlope;
        framebuffer.depthFormat = ToNvrhi(desc.depth->format);
    } else {
        state.depthStencilState.depthTestEnable = false;
        state.depthStencilState.depthWriteEnable = false;
    }
    impl->handle = m_device->createGraphicsPipeline(native, framebuffer);
    if (!impl->handle) {
        Log::GetEngineLogger().Error("Failed to create pipeline");
        return nullptr;
    }
    return GraphicsAccess::Make<Pipeline>(std::move(impl));
}

std::unique_ptr<ComputePipeline> NvrhiDevice::CreateComputePipeline(const ComputePipelineDesc& desc) {
    const auto& shader = static_cast<const NvrhiShader&>(GraphicsAccess::GetImpl(*desc.shader));
    const StageResources& resources = shader.resources;
    auto impl = std::make_unique<NvrhiComputePipeline>();
    impl->shared = m_shared;
    impl->resources = resources;
    for (std::uint32_t set = 0; set < ComputeSetCount; ++set) {
        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::Compute;
        layout.registerSpace = set;
        layout.registerSpaceIsDescriptorSet = true;
        switch (static_cast<ComputeSet>(set)) {
        case ComputeSet::Resources:
            for (std::uint32_t slot = 0; slot < resources.textures; ++slot) {
                layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(slot));
                layout.addItem(nvrhi::BindingLayoutItem::Sampler(slot));
            }
            for (std::uint32_t slot = 0; slot < resources.storageBuffers; ++slot) {
                layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(resources.textures + slot));
            }
            break;
        case ComputeSet::Writes:
            for (std::uint32_t slot = 0; slot < resources.readWriteTextures; ++slot) {
                layout.addItem(nvrhi::BindingLayoutItem::Texture_UAV(slot));
            }
            for (std::uint32_t slot = 0; slot < resources.readWriteBuffers; ++slot) {
                layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(resources.readWriteTextures + slot));
            }
            break;
        default:
            for (std::uint32_t slot = 0; slot < resources.uniformBuffers; ++slot) {
                layout.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(slot));
            }
            break;
        }
        impl->layouts[set] = m_device->createBindingLayout(layout);
    }
    nvrhi::ComputePipelineDesc native;
    native.CS = shader.handle;
    for (const auto& layout : impl->layouts) {
        native.addBindingLayout(layout);
    }
    impl->handle = m_device->createComputePipeline(native);
    if (!impl->handle) {
        Log::GetEngineLogger().Error("Failed to create compute pipeline");
        return nullptr;
    }
    return GraphicsAccess::Make<ComputePipeline>(std::move(impl));
}

std::unique_ptr<Buffer> NvrhiDevice::CreateBuffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        Log::GetEngineLogger().Error("Cannot create an empty buffer");
        return nullptr;
    }
    nvrhi::BufferDesc native;
    native.byteSize = desc.size;
    native.isVertexBuffer = desc.usage == BufferUsage::Vertex;
    native.isIndexBuffer = desc.usage == BufferUsage::Index;
    if (desc.usage == BufferUsage::Storage || desc.usage == BufferUsage::Indirect) {
        // Bound as structured buffers; Vulkan reads them as storage buffers, whatever their elements' size. Compute
        // passes write them.
        native.structStride = 4;
        native.canHaveRawViews = true;
        native.canHaveUAVs = true;
        native.isDrawIndirectArgs = desc.usage == BufferUsage::Indirect;
    }
    native.enableAutomaticStateTracking(desc.usage == BufferUsage::Vertex  ? nvrhi::ResourceStates::VertexBuffer
                                        : desc.usage == BufferUsage::Index ? nvrhi::ResourceStates::IndexBuffer
                                                                           : nvrhi::ResourceStates::ShaderResource);
    native.debugName = desc.usage == BufferUsage::Vertex     ? "Vertex buffer"
                       : desc.usage == BufferUsage::Index    ? "Index buffer"
                       : desc.usage == BufferUsage::Indirect ? "Indirect buffer"
                                                             : "Storage buffer";
    nvrhi::BufferHandle handle = m_device->createBuffer(native);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create buffer");
        return nullptr;
    }
    auto impl = std::make_unique<NvrhiBuffer>();
    impl->shared = m_shared;
    impl->handle = std::move(handle);
    impl->usage = desc.usage;
    impl->size = desc.size;
    return GraphicsAccess::Make<Buffer>(std::move(impl));
}

void NvrhiDevice::UploadToBuffer(Buffer& buffer, std::span<const std::byte> data, std::uint32_t offset,
                                 UploadMode mode) {
    auto& impl = static_cast<NvrhiBuffer&>(GraphicsAccess::GetImpl(buffer));
    const bool discards = mode == UploadMode::Discard || (offset == 0 && data.size() == buffer.GetSize());
    if (discards && m_context.frameOpen) {
        // Draws recorded so far keep the memory they were recorded with, as discarding uploads promise: the buffer
        // moves to other memory, retired memory the GPU is done with or new, which later draws bind instead.
        const std::uint64_t done = m_frameNumber > FramesInFlight ? m_frameNumber - FramesInFlight : 0;
        const auto spare =
            std::ranges::find_if(impl.retired, [done](const auto& entry) { return entry.second <= done; });
        nvrhi::BufferHandle next;
        if (spare != impl.retired.end()) {
            next = std::move(spare->first);
            impl.retired.erase(spare);
        } else {
            next = m_device->createBuffer(impl.handle->getDesc());
        }
        if (next) {
            impl.retired.emplace_back(std::move(impl.handle), m_frameNumber);
            impl.handle = std::move(next);
        }
    }
    // Ahead of the frame being recorded, so its draws, those recorded already included, see the new data.
    nvrhi::IBuffer* native = impl.handle;
    RunNow(
        [&](nvrhi::ICommandList& commandList) { commandList.writeBuffer(native, data.data(), data.size(), offset); });
}

nvrhi::BindingLayoutHandle NvrhiDevice::GetTextureTableLayout() {
    if (!m_textureTableLayout) {
        nvrhi::BindlessLayoutDesc desc;
        desc.visibility = nvrhi::ShaderType::All;
        desc.maxCapacity = GraphicsDevice::MaxTextureTableCapacity;
        desc.addRegisterSpace(nvrhi::BindingLayoutItem::Texture_SRV(4));
        m_textureTableLayout = m_device->createBindlessLayout(desc);
    }
    return m_textureTableLayout;
}

std::unique_ptr<TextureTable> NvrhiDevice::CreateTextureTable(std::uint32_t capacity) {
    nvrhi::DescriptorTableHandle table = m_device->createDescriptorTable(GetTextureTableLayout());
    if (!table) {
        Log::GetEngineLogger().Error("Failed to create a texture table");
        return nullptr;
    }
    m_device->resizeDescriptorTable(table, capacity, false);
    auto impl = std::make_unique<NvrhiTextureTable>();
    impl->shared = m_shared;
    impl->table = std::move(table);
    impl->textures.resize(capacity);
    impl->capacity = capacity;
    return GraphicsAccess::Make<TextureTable>(std::move(impl));
}

void NvrhiDevice::SetTextureTableEntry(TextureTable& table, std::uint32_t index, const Texture& texture) {
    auto& impl = static_cast<NvrhiTextureTable&>(GraphicsAccess::GetImpl(table));
    nvrhi::ITexture* native = GetNvrhiTexture(texture);
    m_device->writeDescriptorTable(impl.table, nvrhi::BindingSetItem::Texture_SRV(index, native));
    impl.textures[index] = native;
}

std::unique_ptr<Sampler> NvrhiDevice::CreateSampler(const SamplerDesc& desc) {
    nvrhi::SamplerDesc native;
    const bool linear = desc.filter == TextureFilter::Linear;
    native.minFilter = linear;
    native.magFilter = linear;
    native.mipFilter = linear;
    native.addressU = ToNvrhi(desc.addressMode);
    native.addressV = native.addressU;
    native.addressW = native.addressU;
    native.maxAnisotropy = linear ? std::clamp(desc.maxAnisotropy, 1.0f, 16.0f) : 1.0f;
    if (desc.compare) {
        // NVRHI compares with less; the engine's shadows ask for less or equal, which only differs where a depth
        // is exactly the one stored.
        native.reductionType = nvrhi::SamplerReductionType::Comparison;
    }
    nvrhi::SamplerHandle handle = m_device->createSampler(native);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create sampler");
        return nullptr;
    }
    auto impl = std::make_unique<NvrhiSampler>();
    impl->shared = m_shared;
    impl->handle = std::move(handle);
    return GraphicsAccess::Make<Sampler>(std::move(impl));
}

} // namespace Spot::Detail
