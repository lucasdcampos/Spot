#include "core/assert.h"
#include "core/log.h"
#include "graphics/sdl_gpu/sdl_gpu_internal.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace Spot::Detail {

namespace {

struct ShaderCode {
    SDL_GPUShaderFormat format;
    std::span<const std::uint8_t> code;
    const char* entryPoint;
};

// Uses the format the device's backend takes. SPIRV-Cross renames the entry point when it translates to MSL, since
// "main" is reserved in Metal.
std::optional<ShaderCode> ChooseShaderCode(SDL_GPUDevice* device, const ShaderDesc& desc) {
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) && !desc.dxil.empty()) {
        return ShaderCode{SDL_GPU_SHADERFORMAT_DXIL, desc.dxil, "main"};
    }
    if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) && !desc.spirv.empty()) {
        return ShaderCode{SDL_GPU_SHADERFORMAT_SPIRV, desc.spirv, "main"};
    }
    if ((formats & SDL_GPU_SHADERFORMAT_MSL) && !desc.msl.empty()) {
        return ShaderCode{SDL_GPU_SHADERFORMAT_MSL, desc.msl, "main0"};
    }
    return std::nullopt;
}

SDL_GPUPrimitiveType ToSdl(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::TriangleList:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    case PrimitiveType::TriangleStrip:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    case PrimitiveType::LineList:
        return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case PrimitiveType::LineStrip:
        return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
    case PrimitiveType::PointList:
        return SDL_GPU_PRIMITIVETYPE_POINTLIST;
    }
    return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

SDL_GPUCullMode ToSdl(CullMode mode) {
    switch (mode) {
    case CullMode::None:
        return SDL_GPU_CULLMODE_NONE;
    case CullMode::Front:
        return SDL_GPU_CULLMODE_FRONT;
    case CullMode::Back:
        return SDL_GPU_CULLMODE_BACK;
    }
    return SDL_GPU_CULLMODE_NONE;
}

SDL_GPUVertexElementFormat ToSdl(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case VertexFormat::Float2:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case VertexFormat::Float3:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case VertexFormat::UInt4:
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT4;
    case VertexFormat::Float4:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case VertexFormat::UByte4Normalized:
        return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

SDL_GPUColorTargetBlendState ToSdl(BlendMode mode) {
    SDL_GPUColorTargetBlendState blend{};
    if (mode == BlendMode::None) {
        return blend;
    }

    blend.enable_blend = true;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_color_blendfactor =
        mode == BlendMode::Premultiplied ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    if (mode == BlendMode::Alpha || mode == BlendMode::Premultiplied) {
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    } else {
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    }
    return blend;
}

SDL_GPUCompareOp ToSdl(CompareOp op) {
    switch (op) {
    case CompareOp::Never:
        return SDL_GPU_COMPAREOP_NEVER;
    case CompareOp::Less:
        return SDL_GPU_COMPAREOP_LESS;
    case CompareOp::LessOrEqual:
        return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case CompareOp::Equal:
        return SDL_GPU_COMPAREOP_EQUAL;
    case CompareOp::Greater:
        return SDL_GPU_COMPAREOP_GREATER;
    case CompareOp::GreaterOrEqual:
        return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    case CompareOp::NotEqual:
        return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case CompareOp::Always:
        return SDL_GPU_COMPAREOP_ALWAYS;
    }
    return SDL_GPU_COMPAREOP_ALWAYS;
}

SDL_GPUFilter ToSdl(TextureFilter filter) {
    return filter == TextureFilter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
}

SDL_GPUSamplerAddressMode ToSdl(TextureAddressMode mode) {
    switch (mode) {
    case TextureAddressMode::ClampToEdge:
        return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    case TextureAddressMode::Repeat:
        return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    case TextureAddressMode::MirroredRepeat:
        return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    }
    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}

std::uint32_t GetLevelSize(std::uint32_t size, std::uint32_t mipLevel) {
    return std::max(size >> mipLevel, 1u);
}

// The bytes of one layer of a mip level.
std::uint32_t GetLayerDataSize(const Texture& texture, std::uint32_t mipLevel = 0) {
    return GetTextureDataSize(texture.GetFormat(), GetLevelSize(texture.GetWidth(), mipLevel),
                              GetLevelSize(texture.GetHeight(), mipLevel));
}

// The size of a copy of a mip level: compressed levels smaller than a block are still copied whole blocks, as
// Direct3D 12 requires.
std::uint32_t GetCopySize(TextureFormat format, std::uint32_t size, std::uint32_t mipLevel) {
    const std::uint32_t level = GetLevelSize(size, mipLevel);
    return IsCompressedFormat(format) ? (level + 3) / 4 * 4 : level;
}

std::uint32_t GetPixelDataSize(const Texture& texture, std::uint32_t mipLevel = 0) {
    return GetLayerDataSize(texture, mipLevel) * texture.GetLayerCount();
}

} // namespace

SDL_GPUTextureFormat ToSdl(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Bgra8:
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case TextureFormat::Rgba8Srgb:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    case TextureFormat::Bgra8Srgb:
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    case TextureFormat::Depth32Float:
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    case TextureFormat::Rgba16Float:
        return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::R11G11B10Float:
        return SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT;
    case TextureFormat::R8:
        return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case TextureFormat::Rg8:
        return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    case TextureFormat::R16Float:
        return SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    case TextureFormat::Rg16Float:
        return SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
    case TextureFormat::R32Float:
        return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    case TextureFormat::Rg32Float:
        return SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
    case TextureFormat::Rgba32Float:
        return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    case TextureFormat::R32Uint:
        return SDL_GPU_TEXTUREFORMAT_R32_UINT;
    case TextureFormat::Rgb10A2:
        return SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM;
    case TextureFormat::Bc1:
        return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case TextureFormat::Bc1Srgb:
        return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB;
    case TextureFormat::Bc3:
        return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case TextureFormat::Bc3Srgb:
        return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB;
    case TextureFormat::Bc4:
        return SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM;
    case TextureFormat::Bc5:
        return SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
    case TextureFormat::Bc6hFloat:
        return SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT;
    case TextureFormat::Bc7:
        return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
    case TextureFormat::Bc7Srgb:
        return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB;
    }
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

std::optional<SDL_GPUSampleCount> ToSdlSampleCount(std::uint32_t sampleCount) {
    switch (sampleCount) {
    case 1:
        return SDL_GPU_SAMPLECOUNT_1;
    case 2:
        return SDL_GPU_SAMPLECOUNT_2;
    case 4:
        return SDL_GPU_SAMPLECOUNT_4;
    case 8:
        return SDL_GPU_SAMPLECOUNT_8;
    default:
        return std::nullopt;
    }
}

std::unique_ptr<Shader> SdlGpuDevice::CreateShader(const ShaderDesc& desc) {
    SDL_GPUDevice* device = shared->Get();
    const auto code = ChooseShaderCode(device, desc);
    if (!code) {
        Log::GetEngineLogger().Error("Shader has no code in a format the '{}' backend accepts", GetBackendName());
        return nullptr;
    }

    if (desc.stage == ShaderStage::Compute) {
        auto impl = std::make_unique<SdlGpuShader>();
        impl->device = shared;
        impl->stage = desc.stage;
        impl->computeCode.assign(code->code.begin(), code->code.end());
        SDL_GPUComputePipelineCreateInfo& info = impl->compute;
        info.entrypoint = code->entryPoint;
        info.format = code->format;
        info.num_samplers = desc.samplerCount;
        info.num_readonly_storage_textures = desc.storageTextureCount;
        info.num_readonly_storage_buffers = desc.storageBufferCount;
        info.num_readwrite_storage_textures = desc.readWriteStorageTextureCount;
        info.num_readwrite_storage_buffers = desc.readWriteStorageBufferCount;
        info.num_uniform_buffers = desc.uniformBufferCount;
        info.threadcount_x = desc.threadCountX;
        info.threadcount_y = desc.threadCountY;
        info.threadcount_z = desc.threadCountZ;
        return GraphicsAccess::Make<Shader>(std::move(impl));
    }

    SDL_GPUShaderCreateInfo info{};
    info.code = code->code.data();
    info.code_size = code->code.size();
    info.entrypoint = code->entryPoint;
    info.format = code->format;
    info.stage = desc.stage == ShaderStage::Vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = desc.samplerCount;
    info.num_storage_textures = desc.storageTextureCount;
    info.num_storage_buffers = desc.storageBufferCount;
    info.num_uniform_buffers = desc.uniformBufferCount;

    SDL_GPUShader* handle = SDL_CreateGPUShader(device, &info);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create shader: {}", SDL_GetError());
        return nullptr;
    }

    auto impl = std::make_unique<SdlGpuShader>();
    impl->device = shared;
    impl->handle = handle;
    impl->stage = desc.stage;
    return GraphicsAccess::Make<Shader>(std::move(impl));
}

std::unique_ptr<Pipeline> SdlGpuDevice::CreatePipeline(const PipelineDesc& desc) {
    SDL_GPUDevice* device = shared->Get();

    std::vector<SDL_GPUColorTargetDescription> colorTargets(1 + desc.additionalTargetFormats.size());
    colorTargets[0].format =
        desc.targetFormat ? ToSdl(*desc.targetFormat) : SDL_GetGPUSwapchainTextureFormat(device, window);
    for (std::size_t i = 0; i < desc.additionalTargetFormats.size(); ++i) {
        colorTargets[i + 1].format = ToSdl(desc.additionalTargetFormats[i]);
    }
    for (SDL_GPUColorTargetDescription& colorTarget : colorTargets) {
        colorTarget.blend_state = ToSdl(desc.blendMode);
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = static_cast<SDL_GPUShader*>(desc.vertexShader->GetNativeHandle());
    info.fragment_shader = static_cast<SDL_GPUShader*>(desc.fragmentShader->GetNativeHandle());
    info.primitive_type = ToSdl(desc.primitiveType);

    std::vector<SDL_GPUVertexBufferDescription> vertexBuffers;
    std::vector<SDL_GPUVertexAttribute> vertexAttributes;
    for (std::uint32_t slot = 0; slot < desc.vertexBuffers.size(); ++slot) {
        const auto& layout = desc.vertexBuffers[slot];
        SDL_GPUVertexBufferDescription buffer{};
        buffer.slot = slot;
        buffer.pitch = layout.stride;
        buffer.input_rate = layout.inputRate == VertexInputRate::Instance ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                                                                          : SDL_GPU_VERTEXINPUTRATE_VERTEX;
        vertexBuffers.push_back(buffer);

        for (const auto& attribute : layout.attributes) {
            vertexAttributes.push_back({attribute.location, slot, ToSdl(attribute.format), attribute.offset});
        }
    }
    info.vertex_input_state.vertex_buffer_descriptions = vertexBuffers.data();
    info.vertex_input_state.num_vertex_buffers = static_cast<Uint32>(vertexBuffers.size());
    info.vertex_input_state.vertex_attributes = vertexAttributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(vertexAttributes.size());
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = ToSdl(desc.cullMode);
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = !(desc.depth && desc.depth->clamp);
    if (!desc.depthOnly) {
        info.target_info.color_target_descriptions = colorTargets.data();
        info.target_info.num_color_targets = static_cast<Uint32>(colorTargets.size());
    }
    if (desc.depth) {
        info.depth_stencil_state.enable_depth_test = desc.depth->test;
        info.depth_stencil_state.enable_depth_write = desc.depth->write;
        info.depth_stencil_state.compare_op = ToSdl(desc.depth->compare);
        info.rasterizer_state.enable_depth_bias = desc.depth->biasConstant != 0.0f || desc.depth->biasSlope != 0.0f;
        info.rasterizer_state.depth_bias_constant_factor = desc.depth->biasConstant;
        info.rasterizer_state.depth_bias_slope_factor = desc.depth->biasSlope;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = ToSdl(desc.depth->format);
    }

    const auto sampleCount = ToSdlSampleCount(desc.sampleCount);
    if (!sampleCount) {
        Log::GetEngineLogger().Error("Pipelines cannot draw with {} samples, only 1, 2, 4 or 8", desc.sampleCount);
        return nullptr;
    }
    info.multisample_state.sample_count = *sampleCount;

    SDL_GPUGraphicsPipeline* handle = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create pipeline: {}", SDL_GetError());
        return nullptr;
    }

    auto impl = std::make_unique<SdlGpuPipeline>();
    impl->device = shared;
    impl->handle = handle;
    return GraphicsAccess::Make<Pipeline>(std::move(impl));
}

std::unique_ptr<ComputePipeline> SdlGpuDevice::CreateComputePipeline(const ComputePipelineDesc& desc) {
    const auto& shader = static_cast<const SdlGpuShader&>(GraphicsAccess::GetImpl(*desc.shader));
    SDL_GPUComputePipelineCreateInfo info = shader.compute;
    info.code = shader.computeCode.data();
    info.code_size = shader.computeCode.size();
    SDL_GPUComputePipeline* handle = SDL_CreateGPUComputePipeline(shared->Get(), &info);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create compute pipeline: {}", SDL_GetError());
        return nullptr;
    }
    auto impl = std::make_unique<SdlGpuComputePipeline>();
    impl->device = shared;
    impl->handle = handle;
    return GraphicsAccess::Make<ComputePipeline>(std::move(impl));
}

std::unique_ptr<Buffer> SdlGpuDevice::CreateBuffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        Log::GetEngineLogger().Error("Cannot create an empty buffer");
        return nullptr;
    }

    SDL_GPUBufferCreateInfo info{};
    // Storage and indirect buffers are read by every stage and written by compute passes.
    constexpr SDL_GPUBufferUsageFlags storage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
                                                SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                                                SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    switch (desc.usage) {
    case BufferUsage::Vertex:
        info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        break;
    case BufferUsage::Index:
        info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        break;
    case BufferUsage::Storage:
        info.usage = storage;
        break;
    case BufferUsage::Indirect:
        info.usage = SDL_GPU_BUFFERUSAGE_INDIRECT | storage;
        break;
    }
    info.size = desc.size;

    SDL_GPUBuffer* handle = SDL_CreateGPUBuffer(shared->Get(), &info);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create buffer: {}", SDL_GetError());
        return nullptr;
    }

    auto impl = std::make_unique<SdlGpuBuffer>();
    impl->device = shared;
    impl->handle = handle;
    impl->usage = desc.usage;
    impl->size = desc.size;
    return GraphicsAccess::Make<Buffer>(std::move(impl));
}

void SdlGpuDevice::UploadToBuffer(Buffer& buffer, std::span<const std::byte> data, std::uint32_t offset,
                                  UploadMode mode) {
    SDL_GPUDevice* device = shared->Get();
    const auto size = static_cast<Uint32>(data.size());

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!transfer) {
        Log::GetEngineLogger().Error("Failed to create transfer buffer: {}", SDL_GetError());
        return;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    std::memcpy(mapped, data.data(), data.size());
    SDL_UnmapGPUTransferBuffer(device, transfer);

    // A separate command buffer submitted now runs before the current frame's, which is submitted later.
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = transfer;
    SDL_GPUBufferRegion destination{};
    destination.buffer = static_cast<SDL_GPUBuffer*>(buffer.GetNativeHandle());
    destination.offset = offset;
    destination.size = size;
    // Cycling makes SDL write into fresh memory instead of waiting for draws still reading the buffer, at the cost of
    // the data outside the upload.
    const bool cycle = mode == UploadMode::Discard || (offset == 0 && size == buffer.GetSize());
    SDL_UploadToGPUBuffer(copyPass, &source, &destination, cycle);

    SDL_EndGPUCopyPass(copyPass);
    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        Log::GetEngineLogger().Error("Failed to submit buffer upload: {}", SDL_GetError());
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
}

std::unique_ptr<Texture> SdlGpuDevice::CreateTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0) {
        Log::GetEngineLogger().Error("Cannot create a {}x{} texture", desc.width, desc.height);
        return nullptr;
    }
    const bool depth = IsDepthFormat(desc.format);
    if (depth && desc.mipmaps) {
        Log::GetEngineLogger().Error("Depth textures cannot have mipmaps");
        return nullptr;
    }
    const auto sampleCount = ToSdlSampleCount(desc.sampleCount);
    if (!sampleCount) {
        Log::GetEngineLogger().Error("Textures cannot have {} samples, only 1, 2, 4 or 8", desc.sampleCount);
        return nullptr;
    }
    const bool multisampled = desc.sampleCount > 1;
    if (multisampled && (!desc.renderTarget || desc.mipmaps)) {
        Log::GetEngineLogger().Error("Multisampled textures must be render targets without mipmaps");
        return nullptr;
    }
    if (!SupportsSampleCount(desc.format, desc.sampleCount)) {
        Log::GetEngineLogger().Error("This GPU does not support {} samples for this texture format", desc.sampleCount);
        return nullptr;
    }
    const bool cube = desc.type == TextureType::Cube;
    if (cube && (desc.width != desc.height || depth || multisampled)) {
        Log::GetEngineLogger().Error("Cube textures must be square, and neither depth textures nor multisampled");
        return nullptr;
    }

    // Each level halves the size of the previous one, down to 1x1.
    std::uint32_t mipLevels = 1;
    if (desc.mipmaps) {
        for (std::uint32_t size = std::max(desc.width, desc.height); size > 1; size /= 2) {
            ++mipLevels;
        }
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = cube ? SDL_GPU_TEXTURETYPE_CUBE : SDL_GPU_TEXTURETYPE_2D;
    info.format = ToSdl(desc.format);
    // Multisampled textures are only ever resolved, never sampled.
    info.usage = multisampled ? 0 : SDL_GPU_TEXTUREUSAGE_SAMPLER;
    // SDL generates mipmaps by rendering each level from the previous one, so they need a color target.
    if (depth && desc.renderTarget) {
        info.usage |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    } else if (desc.renderTarget || (mipLevels > 1 && !IsCompressedFormat(desc.format))) {
        info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    }
    if (desc.storage) {
        info.usage |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
    }
    info.width = desc.width;
    info.height = desc.height;
    info.layer_count_or_depth = cube ? 6 : 1;
    info.num_levels = mipLevels;
    info.sample_count = *sampleCount;

    SDL_GPUTexture* handle = SDL_CreateGPUTexture(shared->Get(), &info);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create texture: {}", SDL_GetError());
        return nullptr;
    }

    auto impl = std::make_unique<SdlGpuTexture>();
    impl->device = shared;
    impl->handle = handle;
    impl->desc = desc;
    impl->mipLevels = mipLevels;
    return GraphicsAccess::Make<Texture>(std::move(impl));
}

std::unique_ptr<Sampler> SdlGpuDevice::CreateSampler(const SamplerDesc& desc) {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = ToSdl(desc.filter);
    info.mag_filter = ToSdl(desc.filter);
    info.mipmap_mode =
        desc.filter == TextureFilter::Nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = ToSdl(desc.addressMode);
    info.address_mode_v = ToSdl(desc.addressMode);
    info.address_mode_w = ToSdl(desc.addressMode);
    // Left at zero, the highest level of detail would hold every texture to its full size, ignoring its mipmaps.
    info.max_lod = 1000.0f;
    if (desc.filter == TextureFilter::Linear && desc.maxAnisotropy > 1.0f) {
        info.enable_anisotropy = true;
        info.max_anisotropy = std::min(desc.maxAnisotropy, 16.0f);
    }
    if (desc.compare) {
        info.enable_compare = true;
        info.compare_op = ToSdl(*desc.compare);
    }

    SDL_GPUSampler* handle = SDL_CreateGPUSampler(shared->Get(), &info);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create sampler: {}", SDL_GetError());
        return nullptr;
    }

    auto impl = std::make_unique<SdlGpuSampler>();
    impl->device = shared;
    impl->handle = handle;
    return GraphicsAccess::Make<Sampler>(std::move(impl));
}

void SdlGpuDevice::UploadToTexture(Texture& texture, std::uint32_t mipLevel, std::span<const std::byte> pixels,
                                   bool generateMipmaps) {
    SDL_GPUDevice* device = shared->Get();
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(pixels.size());
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!transfer) {
        Log::GetEngineLogger().Error("Failed to create transfer buffer: {}", SDL_GetError());
        return;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    std::memcpy(mapped, pixels.data(), pixels.size());
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

    SDL_GPUTextureRegion destination{};
    destination.texture = static_cast<SDL_GPUTexture*>(texture.GetNativeHandle());
    destination.mip_level = mipLevel;
    destination.w = GetCopySize(texture.GetFormat(), texture.GetWidth(), mipLevel);
    destination.h = GetCopySize(texture.GetFormat(), texture.GetHeight(), mipLevel);
    destination.d = 1;
    // Every pixel is replaced when the whole texture is, so draws still reading the old contents can keep them while
    // this writes new memory. Only the first copy may do that, or it would throw away the layers before it, and only
    // a texture that has no other levels to keep or regenerates them all.
    const bool replacesAll = mipLevel == 0 && (texture.GetMipLevelCount() == 1 || generateMipmaps);
    for (std::uint32_t layer = 0; layer < texture.GetLayerCount(); ++layer) {
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.offset = layer * GetLayerDataSize(texture, mipLevel);
        source.pixels_per_row = destination.w;
        source.rows_per_layer = destination.h;
        destination.layer = layer;
        SDL_UploadToGPUTexture(copyPass, &source, &destination, replacesAll && layer == 0);
    }

    SDL_EndGPUCopyPass(copyPass);
    if (generateMipmaps) {
        SDL_GenerateMipmapsForGPUTexture(commandBuffer, destination.texture);
    }
    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        Log::GetEngineLogger().Error("Failed to submit texture upload: {}", SDL_GetError());
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
}

std::vector<std::uint8_t> SdlGpuDevice::DownloadTexture(const Texture& texture, std::uint32_t mipLevel) {
    SDL_GPUDevice* device = shared->Get();
    const std::uint32_t size = GetPixelDataSize(texture, mipLevel);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!transfer) {
        Log::GetEngineLogger().Error("Failed to create transfer buffer: {}", SDL_GetError());
        return {};
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

    SDL_GPUTextureRegion source{};
    source.texture = static_cast<SDL_GPUTexture*>(texture.GetNativeHandle());
    source.mip_level = mipLevel;
    source.w = GetCopySize(texture.GetFormat(), texture.GetWidth(), mipLevel);
    source.h = GetCopySize(texture.GetFormat(), texture.GetHeight(), mipLevel);
    source.d = 1;
    for (std::uint32_t layer = 0; layer < texture.GetLayerCount(); ++layer) {
        source.layer = layer;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = transfer;
        destination.offset = layer * GetLayerDataSize(texture, mipLevel);
        destination.pixels_per_row = source.w;
        destination.rows_per_layer = source.h;
        SDL_DownloadFromGPUTexture(copyPass, &source, &destination);
    }

    SDL_EndGPUCopyPass(copyPass);
    std::vector<std::uint8_t> pixels;
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
    if (fence && SDL_WaitForGPUFences(device, true, &fence, 1)) {
        const auto* mapped = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(device, transfer, false));
        pixels.assign(mapped, mapped + size);
        SDL_UnmapGPUTransferBuffer(device, transfer);
    } else {
        Log::GetEngineLogger().Error("Failed to download texture: {}", SDL_GetError());
    }
    if (fence) {
        SDL_ReleaseGPUFence(device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return pixels;
}

} // namespace Spot::Detail
