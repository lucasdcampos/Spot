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
    if (desc.samplerCount > MaxStageTextures || desc.storageBufferCount > MaxStageStorageBuffers ||
        desc.uniformBufferCount > MaxStageUniforms) {
        Log::GetEngineLogger().Error("Shader reads more resources than a stage can bind");
        return nullptr;
    }
    nvrhi::ShaderDesc native;
    native.shaderType = desc.stage == ShaderStage::Vertex ? nvrhi::ShaderType::Vertex : nvrhi::ShaderType::Pixel;
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
    impl->resources = {desc.samplerCount, desc.storageBufferCount, desc.uniformBufferCount};
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
    nvrhi::RenderState& state = native.renderState;
    state.blendState.targets[0] = ToNvrhi(desc.blendMode);
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
    }
    if (desc.depth) {
        state.depthStencilState.depthTestEnable = desc.depth->test;
        state.depthStencilState.depthWriteEnable = desc.depth->write;
        state.depthStencilState.depthFunc = ToNvrhi(desc.depth->compare);
        state.rasterState.depthBias = static_cast<int>(desc.depth->biasConstant);
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

std::unique_ptr<Buffer> NvrhiDevice::CreateBuffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        Log::GetEngineLogger().Error("Cannot create an empty buffer");
        return nullptr;
    }
    nvrhi::BufferDesc native;
    native.byteSize = desc.size;
    native.isVertexBuffer = desc.usage == BufferUsage::Vertex;
    native.isIndexBuffer = desc.usage == BufferUsage::Index;
    if (desc.usage == BufferUsage::Storage) {
        // Bound as structured buffers; Vulkan reads them as storage buffers, whatever their elements' size.
        native.structStride = 4;
        native.canHaveRawViews = true;
    }
    native.enableAutomaticStateTracking(desc.usage == BufferUsage::Vertex  ? nvrhi::ResourceStates::VertexBuffer
                                        : desc.usage == BufferUsage::Index ? nvrhi::ResourceStates::IndexBuffer
                                                                           : nvrhi::ResourceStates::ShaderResource);
    native.debugName = desc.usage == BufferUsage::Vertex  ? "Vertex buffer"
                       : desc.usage == BufferUsage::Index ? "Index buffer"
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
    nvrhi::IBuffer* native = GetNvrhiBuffer(buffer);
    const bool discards = mode == UploadMode::Discard || (offset == 0 && data.size() == buffer.GetSize());
    if (discards && m_context.frameOpen) {
        // In the frame's own commands, after the draws recorded so far, which keep reading what they were recorded
        // with, as discarding uploads promise.
        m_context.commandList->writeBuffer(native, data.data(), data.size(), offset);
        return;
    }
    // Ahead of the frame being recorded, so its draws, those recorded already included, see the new data.
    RunNow([&](nvrhi::ICommandList& commandList) { commandList.writeBuffer(native, data.data(), data.size(), offset); });
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
