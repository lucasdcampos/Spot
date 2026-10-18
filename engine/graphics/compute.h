#pragma once

#include "graphics/buffer.h"
#include "graphics/texture.h"

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace Spot {

namespace Detail {
struct GraphicsAccess;
} // namespace Detail

class Shader;

struct ComputePipelineDesc {
    // A shader of ShaderStage::Compute, whose thread group size it was compiled with.
    const Shader* shader = nullptr;
};

// A compute shader ready to dispatch. Like graphics pipelines, create them ahead of time.
class ComputePipeline {
public:
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    // The backend's pipeline (an SDL_GPUComputePipeline* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend struct Detail::GraphicsAccess;

    explicit ComputePipeline(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

// A mip level of a 2D texture created with TextureDesc::storage, which the pass's shaders write.
struct StorageTexture {
    Texture* texture = nullptr;
    std::uint32_t mipLevel = 0;
};

// What a compute pass writes, named when it begins: shaders may write these and read them back during the pass, and
// later passes see what they wrote.
struct ComputePassDesc {
    // RWTexture2D at register u{index} of space1.
    std::vector<StorageTexture> textures = {};
    // RWStructuredBuffer or RWByteAddressBuffer at register u{textures.size() + index} of space1: storage buffers, or
    // indirect buffers whose draw or dispatch arguments the shader writes.
    std::vector<Buffer*> buffers = {};
};

// Commands that run compute shaders. Like render passes, only one pass of either kind is open at a time, and it ends
// when End() is called or when it goes out of scope.
//
// Shaders declare what they read in space0: textures and their samplers at t{n} and s{n}, then storage buffers they
// only read at t{textures + n}; what the pass writes in space1 (see ComputePassDesc); and uniforms at b{n} of space2.
class ComputePass {
public:
    ~ComputePass();

    ComputePass(const ComputePass&) = delete;
    ComputePass& operator=(const ComputePass&) = delete;

    void End();

    void BindPipeline(const ComputePipeline& pipeline);
    // Binds the texture and sampler read at register t{slot} and s{slot} of space0.
    void BindTexture(std::uint32_t slot, const Texture& texture, const Sampler& sampler);
    // Binds a storage buffer read, not written, by the shader's StructuredBuffer at t{textures + slot} of space0.
    void BindStorageBuffer(std::uint32_t slot, const Buffer& buffer);

    // Uniforms set the constant buffer at register b{slot} of space2, and stay set for later dispatches of the pass.
    void PushUniformData(std::uint32_t slot, const void* data, std::uint32_t size);

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void PushUniforms(std::uint32_t slot, const T& data) {
        PushUniformData(slot, &data, sizeof(T));
    }

    // Runs this many thread groups, each of the size the shader declares with [numthreads].
    void Dispatch(std::uint32_t groupsX, std::uint32_t groupsY = 1, std::uint32_t groupsZ = 1);
    // Runs the thread groups an indirect buffer holds, as DispatchIndirectArguments at the byte offset, so a
    // dispatch can size the work of the next.
    void DispatchIndirect(const Buffer& buffer, std::uint32_t offset = 0);

    // The backend's pass object (an SDL_GPUComputePass* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend class RenderContext;

    explicit ComputePass(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
    bool m_pipelineBound = false;
};

} // namespace Spot
