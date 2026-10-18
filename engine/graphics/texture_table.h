#pragma once

#include <cstdint>
#include <memory>

namespace Spot {

namespace Detail {
struct GraphicsAccess;
} // namespace Detail

// Textures a shader indexes freely ("bindless"): one table bound for a whole pass, holding every texture its draws
// read, each picked by a number the draw's data holds, so draws need no texture bound of their own and differ only in
// data, as GPU-driven rendering needs. Where GraphicsCapabilities::bindlessTextures says so.
//
// Pipelines made with PipelineDesc::textureTable read it in their fragment shader as
//     Texture2D<float4> textures[] : register(t0, space4);
// loading texels by index (textures[i].Load(...)), or sampling them with a sampler the shader binds as usual.
class TextureTable {
public:
    ~TextureTable();

    TextureTable(const TextureTable&) = delete;
    TextureTable& operator=(const TextureTable&) = delete;

    std::uint32_t GetCapacity() const;

    // The backend's table (an nvrhi::IDescriptorTable* with Vulkan), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend struct Detail::GraphicsAccess;

    explicit TextureTable(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
