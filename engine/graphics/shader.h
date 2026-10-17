#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace Spot {

namespace Detail {
struct GraphicsAccess;
} // namespace Detail

enum class ShaderStage {
    Vertex,
    Fragment,
};

// A compiled shader in every format a backend may need; the device uses the one its graphics API takes. The
// spot_add_shaders() CMake function generates these from HLSL, resource counts included, but they can also be
// filled by hand.
//
// Resources follow SDL_GPU's binding conventions, written in HLSL as:
//   vertex:   textures/samplers t#/s# space0, uniforms b# space1
//   fragment: textures/samplers t#/s# space2, uniforms b# space3
// The fragment shader's input must declare the same fields, in the same order, as the vertex shader's output,
// SV_Position included: Direct3D 12 rejects pipelines whose stages disagree.
struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::span<const std::uint8_t> spirv = {};
    // SPIR-V for the Vulkan backend through NVRHI: textures and samplers apart, each register space a descriptor set,
    // and bindings offset by resource type as NVRHI's default VulkanBindingOffsets number them (shader resources from
    // 0, samplers from 128, constant buffers from 256).
    std::span<const std::uint8_t> vulkanSpirv = {};
    // Metal Shading Language source.
    std::span<const std::uint8_t> msl = {};
    std::span<const std::uint8_t> dxil = {};
    std::uint32_t samplerCount = 0;
    std::uint32_t storageTextureCount = 0;
    std::uint32_t storageBufferCount = 0;
    std::uint32_t uniformBufferCount = 0;
};

class Shader {
public:
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    ShaderStage GetStage() const;

    // The backend's shader (an SDL_GPUShader* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend struct Detail::GraphicsAccess;

    explicit Shader(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
