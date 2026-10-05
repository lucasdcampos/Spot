#pragma once

#include <cstdint>
#include <memory>

namespace Spot {

enum class TextureFormat {
    // 8 bits per channel, in memory order.
    Rgba8,
    Bgra8,
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8;
    // Lets render passes draw into the texture, as well as shaders sample it.
    bool renderTarget = false;
};

// A 2D image on the GPU. Fill it with GraphicsDevice::UploadToTexture() or by rendering into it.
class Texture {
public:
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    std::uint32_t GetWidth() const;
    std::uint32_t GetHeight() const;
    TextureFormat GetFormat() const;
    bool IsRenderTarget() const;

    // The backend's texture (an SDL_GPUTexture* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

private:
    friend class GraphicsDevice;

    struct Impl;

    explicit Texture(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

enum class TextureFilter {
    Linear,
    // Keeps texels sharp when magnified, as pixel art needs.
    Nearest,
};

enum class TextureAddressMode {
    ClampToEdge,
    Repeat,
    MirroredRepeat,
};

struct SamplerDesc {
    TextureFilter filter = TextureFilter::Linear;
    TextureAddressMode addressMode = TextureAddressMode::ClampToEdge;
};

// How shaders read a texture: filtering and what happens outside [0, 1].
class Sampler {
public:
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // The backend's sampler (an SDL_GPUSampler* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

private:
    friend class GraphicsDevice;

    struct Impl;

    explicit Sampler(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
