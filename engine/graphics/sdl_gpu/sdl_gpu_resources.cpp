#include "core/assert.h"
#include "core/log.h"
#include "graphics/sdl_gpu/sdl_gpu_internal.h"

#include <span>

namespace Spot {

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

SDL_GPUColorTargetBlendState ToSdl(BlendMode mode) {
    SDL_GPUColorTargetBlendState blend{};
    if (mode == BlendMode::None) {
        return blend;
    }

    blend.enable_blend = true;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    if (mode == BlendMode::Alpha) {
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    } else {
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    }
    return blend;
}

} // namespace

std::unique_ptr<Shader> GraphicsDevice::CreateShader(const ShaderDesc& desc) {
    SDL_GPUDevice* device = m_impl->device->Get();
    const auto code = ChooseShaderCode(device, desc);
    if (!code) {
        Log::GetEngineLogger().Error("Shader has no code in a format the '{}' backend accepts", GetBackendName());
        return nullptr;
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

    auto impl = std::make_unique<Shader::Impl>();
    impl->device = m_impl->device;
    impl->handle = handle;
    impl->stage = desc.stage;
    return std::unique_ptr<Shader>(new Shader(std::move(impl)));
}

std::unique_ptr<Pipeline> GraphicsDevice::CreatePipeline(const PipelineDesc& desc) {
    SPOT_ASSERT(desc.vertexShader && desc.vertexShader->GetStage() == ShaderStage::Vertex,
                "A pipeline needs a vertex shader");
    SPOT_ASSERT(desc.fragmentShader && desc.fragmentShader->GetStage() == ShaderStage::Fragment,
                "A pipeline needs a fragment shader");

    SDL_GPUDevice* device = m_impl->device->Get();

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, m_impl->window);
    colorTarget.blend_state = ToSdl(desc.blendMode);

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = static_cast<SDL_GPUShader*>(desc.vertexShader->GetNativeHandle());
    info.fragment_shader = static_cast<SDL_GPUShader*>(desc.fragmentShader->GetNativeHandle());
    info.primitive_type = ToSdl(desc.primitiveType);
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = ToSdl(desc.cullMode);
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1;

    SDL_GPUGraphicsPipeline* handle = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create pipeline: {}", SDL_GetError());
        return nullptr;
    }

    auto impl = std::make_unique<Pipeline::Impl>();
    impl->device = m_impl->device;
    impl->handle = handle;
    return std::unique_ptr<Pipeline>(new Pipeline(std::move(impl)));
}

Shader::Shader(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

Shader::~Shader() = default;

ShaderStage Shader::GetStage() const {
    return m_impl->stage;
}

void* Shader::GetNativeHandle() const {
    return m_impl->handle;
}

Pipeline::Pipeline(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

Pipeline::~Pipeline() = default;

void* Pipeline::GetNativeHandle() const {
    return m_impl->handle;
}

} // namespace Spot
