#pragma once

#include <cstdint>
#include <memory>
#include <optional>

namespace Spot {

namespace Detail {
struct GraphicsAccess;
} // namespace Detail

enum class TextureFormat {
    // 8 bits per channel, in memory order, holding the values shaders read and write as they are.
    Rgba8,
    Bgra8,
    // 8 bits per channel, sRGB-encoded: shaders read and write linear values, stored with more precision in the
    // darks, as colors meant for display are, such as images and the window. Alpha is stored as it is.
    Rgba8Srgb,
    Bgra8Srgb,
    // 32-bit float depth, for depth targets. Its pixels cannot be uploaded or downloaded.
    Depth32Float,
    // 16-bit floats per channel, half precision, as HDR render targets need: values above 1 and fine steps in the
    // darks. Pixels are 8 bytes, each channel an IEEE half float.
    Rgba16Float,
    // Unsigned floats packed in 32 bits, 11 bits for red and green and 10 for blue, without alpha: HDR color in half
    // the memory of Rgba16Float. Not every GPU renders into it; see GraphicsDevice::SupportsRenderTarget().
    R11G11B10Float,
    // Fewer channels, for data rather than colors: masks, coverage, velocities, depths of their own, as G-buffers and
    // compute passes use. Shaders read the missing channels as 0, and alpha as 1.
    R8,
    Rg8,
    R16Float,
    Rg16Float,
    R32Float,
    Rg32Float,
    Rgba32Float,
    // A 32-bit unsigned integer, read in HLSL as Texture2D<uint>; it cannot be filtered or blended, only loaded, and
    // not every backend samples it (see GraphicsDevice::SupportsSampling()).
    R32Uint,
    // 10 bits for each color channel and 2 for alpha, unsigned normalized: more precision than Rgba8 in the same
    // memory, as normals and HDR-ish targets want.
    Rgb10A2,
    // Block-compressed formats, which store 4x4 pixels in 8 or 16 bytes and are decoded by the GPU as it samples, so
    // textures take four to eight times less memory. They can be sampled, uploaded and downloaded, but not drawn into,
    // and their mipmaps are uploaded level by level instead of generated. See GraphicsCapabilities::compressedTextures.
    //   Bc1 (8 bytes):  RGB with 1-bit alpha, for opaque colors
    //   Bc3 (16 bytes): RGBA, colors with smooth alpha
    //   Bc4 (8 bytes):  one channel, for masks and heights
    //   Bc5 (16 bytes): two channels, for normal maps
    //   Bc6hFloat (16): RGB unsigned half floats, for HDR images such as skies
    //   Bc7 (16 bytes): RGBA at the best quality, for colors
    Bc1,
    Bc1Srgb,
    Bc3,
    Bc3Srgb,
    Bc4,
    Bc5,
    Bc6hFloat,
    Bc7,
    Bc7Srgb,
};

constexpr bool IsDepthFormat(TextureFormat format) {
    return format == TextureFormat::Depth32Float;
}

constexpr bool IsSrgbFormat(TextureFormat format) {
    return format == TextureFormat::Rgba8Srgb || format == TextureFormat::Bgra8Srgb ||
           format == TextureFormat::Bc1Srgb || format == TextureFormat::Bc3Srgb || format == TextureFormat::Bc7Srgb;
}

constexpr bool IsFloatFormat(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba16Float:
    case TextureFormat::R11G11B10Float:
    case TextureFormat::R16Float:
    case TextureFormat::Rg16Float:
    case TextureFormat::R32Float:
    case TextureFormat::Rg32Float:
    case TextureFormat::Rgba32Float:
    case TextureFormat::Bc6hFloat:
        return true;
    default:
        return false;
    }
}

constexpr bool IsCompressedFormat(TextureFormat format) {
    return format >= TextureFormat::Bc1;
}

// The bytes of one pixel, as uploads and downloads hold them; for compressed formats, of one 4x4 block.
constexpr std::uint32_t GetBytesPerPixel(TextureFormat format) {
    switch (format) {
    case TextureFormat::R8:
        return 1;
    case TextureFormat::Rg8:
    case TextureFormat::R16Float:
        return 2;
    case TextureFormat::Rgba16Float:
    case TextureFormat::Rg32Float:
    case TextureFormat::Bc1:
    case TextureFormat::Bc1Srgb:
    case TextureFormat::Bc4:
        return 8;
    case TextureFormat::Rgba32Float:
    case TextureFormat::Bc3:
    case TextureFormat::Bc3Srgb:
    case TextureFormat::Bc5:
    case TextureFormat::Bc6hFloat:
    case TextureFormat::Bc7:
    case TextureFormat::Bc7Srgb:
        return 16;
    default:
        return 4;
    }
}

// The bytes of one layer of a width x height image of the format, as uploads and downloads hold it: rows of pixels,
// or for compressed formats rows of 4x4 blocks, the last ones covering the edges.
constexpr std::uint32_t GetTextureDataSize(TextureFormat format, std::uint32_t width, std::uint32_t height) {
    if (IsCompressedFormat(format)) {
        return ((width + 3) / 4) * ((height + 3) / 4) * GetBytesPerPixel(format);
    }
    return width * height * GetBytesPerPixel(format);
}

enum class TextureType {
    Texture2D,
    // Six square faces around a point, which shaders sample by direction (TextureCube in HLSL), as environment maps
    // are. The faces are layers 0 to 5, facing +X, -X, +Y, -Y, +Z and -Z, laid out as Direct3D, Vulkan and Metal
    // agree: a face's texel at (s, t), from 0 to 1 with t going down, holds the direction
    // forward + (2s - 1) * right + (1 - 2t) * up, where forward, right and up are:
    //
    //     +X: (1, 0, 0), (0, 0, -1), (0, 1, 0)       -X: (-1, 0, 0), (0, 0, 1), (0, 1, 0)
    //     +Y: (0, 1, 0), (1, 0, 0), (0, 0, -1)       -Y: (0, -1, 0), (1, 0, 0), (0, 0, 1)
    //     +Z: (0, 0, 1), (1, 0, 0), (0, 1, 0)        -Z: (0, 0, -1), (-1, 0, 0), (0, 1, 0)
    Cube,
    // A volume of TextureDesc::depth slices of width x height, which shaders sample by three coordinates from 0 to 1
    // (Texture3D in HLSL), filtering between slices too, as noise and lookup tables for volumes are. Uploads hold its
    // slices one after the other, from z = 0, as they hold layers. Only sampled: never a render target, storage,
    // multisampled, a depth or compressed texture, nor mipmapped.
    Texture3D,
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8;
    // Cube textures must be square, and cannot be depth textures or multisampled.
    TextureType type = TextureType::Texture2D;
    // The slices of a 3D texture; other textures have 1.
    std::uint32_t depth = 1;
    // Lets render passes draw into the texture, as well as shaders sample it. Depth textures become depth targets.
    bool renderTarget = false;
    // Adds a chain of smaller copies of the image, regenerated by every upload, so textures drawn small or at a
    // distance stay smooth instead of shimmering. Color formats only. Render targets can also be drawn into level by
    // level (RenderPassDesc::targetMipLevel), or have their levels made from the first with
    // RenderContext::GenerateMipmaps().
    bool mipmaps = false;
    // 1 for a regular texture; 2, 4 or 8 for a multisampled render target, which smooths the edges of what is drawn
    // into it once a render pass resolves it into a regular texture or the window. Multisampled textures cannot be
    // sampled, uploaded to or downloaded. See GraphicsDevice::SupportsSampleCount().
    std::uint32_t sampleCount = 1;
    // Lets compute passes write the texture (ComputePassDesc::textures), as well as shaders sample it. 2D textures of
    // uncompressed color formats only, and not every format everywhere: the 8-bit RGBA, float and R32Uint ones are
    // safe. Shaders declare the format of what they write, as glslang's [[spv::format_rgba8]] and the like.
    bool storage = false;
};

// An image on the GPU, 2D or a cube. Fill it with GraphicsDevice::UploadToTexture() or by rendering into it.
class Texture {
public:
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    std::uint32_t GetWidth() const;
    std::uint32_t GetHeight() const;
    TextureFormat GetFormat() const;
    TextureType GetType() const;
    // The slices of a 3D texture, 1 for the others.
    std::uint32_t GetDepth() const;
    // What uploads hold one after the other: 6 faces for cube textures, the slices of 3D ones, 1 for the others.
    std::uint32_t GetLayerCount() const;
    bool IsRenderTarget() const;
    bool IsStorage() const;
    // 1 for textures without mipmaps.
    std::uint32_t GetMipLevelCount() const;
    std::uint32_t GetSampleCount() const;

    // The backend's texture (an SDL_GPUTexture* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend struct Detail::GraphicsAccess;

    explicit Texture(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

// How a depth test or a comparison sampler compares a value with the one stored.
enum class CompareOp {
    Never,
    Less,
    LessOrEqual,
    Equal,
    Greater,
    GreaterOrEqual,
    NotEqual,
    Always,
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
    // Above 1, linear filtering takes up to this many samples, at most 16, along the direction a texture is squeezed
    // in, which keeps surfaces seen at a grazing angle, like distant floors, sharp instead of blurred or shimmering.
    float maxAnisotropy = 1.0f;
    // Makes a comparison sampler for depth textures, as shadow maps need: instead of the depth, HLSL's
    // SampleCmp() and SampleCmpLevelZero() on a SamplerComparisonState return how much of the footprint passes
    // `reference <op> stored`, from 0 to 1, blending the nearest texels with linear filtering.
    std::optional<CompareOp> compare = std::nullopt;
};

// How shaders read a texture: filtering and what happens outside [0, 1].
class Sampler {
public:
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // The backend's sampler (an SDL_GPUSampler* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend struct Detail::GraphicsAccess;

    explicit Sampler(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
