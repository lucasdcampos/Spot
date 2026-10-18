// The graphics API's features beyond drawing, on every backend this machine runs: formats, compressed textures, and
// what GraphicsCapabilities tells apart.
#include "textured.frag.h"
#include "textured_quad.vert.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// Runs the test on an offscreen device of each backend built in and supported here.
template <typename Test>
void ForEachBackend(const Test& test) {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No GPU backend is available on this machine");
    }
    for (const auto backend : {Spot::GraphicsBackend::SdlGpu, Spot::GraphicsBackend::Vulkan}) {
        auto device = Spot::GraphicsDevice::CreateOffscreen({.backend = backend, .debug = false});
        if (!device) {
            // Vulkan through NVRHI is only built on Windows and Linux, and needs Vulkan 1.3.
            REQUIRE(backend == Spot::GraphicsBackend::Vulkan);
            continue;
        }
        INFO("Backend " << std::string(device->GetBackendName()));
        test(*device);
    }
}

// Draws the texture over the whole of a 4x4 RGBA8 target, and reads the target back.
std::vector<std::uint8_t> DrawTexture(Spot::GraphicsDevice& device, const Spot::Texture& texture) {
    struct Vertex {
        Spot::Vector2 position;
        Spot::Vector2 uv;
    };
    const std::array<Vertex, 4> vertices{{
        {{-1.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 1.0f}, {1.0f, 0.0f}},
        {{1.0f, -1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    }};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};
    auto target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
    const auto sampler = device.CreateSampler({.filter = Spot::TextureFilter::Nearest});
    const auto vertexBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, vertices);
    const auto indexBuffer = device.CreateBuffer(Spot::BufferUsage::Index, indices);
    const auto vertexShader = device.CreateShader(Shaders::TexturedQuadVert);
    const auto fragmentShader = device.CreateShader(Shaders::TexturedFrag);
    REQUIRE((target && sampler && vertexBuffer && indexBuffer && vertexShader && fragmentShader));
    const auto pipeline = device.CreatePipeline({
        .vertexShader = vertexShader.get(),
        .fragmentShader = fragmentShader.get(),
        .vertexBuffers = {{
            .stride = sizeof(Vertex),
            .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                           {1, Spot::VertexFormat::Float2, offsetof(Vertex, uv)}},
        }},
        .targetFormat = Spot::TextureFormat::Rgba8,
    });
    REQUIRE(pipeline);
    auto* context = device.BeginOffscreenFrame(*target);
    REQUIRE(context);
    {
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
        pass.BindPipeline(*pipeline);
        pass.BindVertexBuffer(0, *vertexBuffer);
        pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
        pass.BindFragmentTexture(0, texture, *sampler);
        pass.PushFragmentUniforms(0, Spot::Color::White);
        pass.DrawIndexed(6);
    }
    device.EndFrame();
    return device.DownloadTexture(*target);
}

std::uint16_t ToHalf(float value) {
    // Exact for the values the tests use, which halves hold without rounding.
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t exponent = ((bits >> 23) & 0xFF) - 127 + 15;
    return static_cast<std::uint16_t>(((bits >> 16) & 0x8000) | (exponent << 10) | ((bits >> 13) & 0x3FF));
}

template <typename T>
T Read(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

} // namespace

TEST_CASE("Textures of every uncompressed format keep the pixels uploaded to them", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        for (const auto format :
             {Spot::TextureFormat::R8, Spot::TextureFormat::Rg8, Spot::TextureFormat::R16Float,
              Spot::TextureFormat::Rg16Float, Spot::TextureFormat::R32Float, Spot::TextureFormat::Rg32Float,
              Spot::TextureFormat::Rgba32Float, Spot::TextureFormat::R32Uint, Spot::TextureFormat::Rgb10A2}) {
            INFO("Format " << static_cast<int>(format));
            // Integer formats cannot be sampled through SDL_GPU on Direct3D 12, which only filters textures.
            if (format == Spot::TextureFormat::R32Uint && !device.SupportsSampling(format)) {
                continue;
            }
            REQUIRE(device.SupportsSampling(format));
            auto texture = device.CreateTexture({.width = 2, .height = 2, .format = format});
            REQUIRE(texture);
            // Bytes below 64 keep floats finite.
            std::vector<std::uint8_t> pixels(Spot::GetTextureDataSize(format, 2, 2));
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                pixels[i] = static_cast<std::uint8_t>(i * 7 % 64);
            }
            device.UploadToTexture(*texture, pixels);
            CHECK(device.DownloadTexture(*texture) == pixels);
        }
    });
}

TEST_CASE("Render targets of the data formats hold what passes clear them to", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        const Spot::Color clear{0.25f, 0.5f, 0.75f, 1.0f};
        const auto render = [&](Spot::TextureFormat format) {
            auto target = device.CreateTexture({.width = 2, .height = 2, .format = format, .renderTarget = true});
            REQUIRE(target);
            auto* context = device.BeginOffscreenFrame(*target);
            REQUIRE(context);
            context->BeginRenderPass({.clearColor = clear}).End();
            device.EndFrame();
            return device.DownloadTexture(*target);
        };
        CHECK(render(Spot::TextureFormat::R8)[0] == 64);
        const auto rg8 = render(Spot::TextureFormat::Rg8);
        // 0.5 of 255 rounds either way.
        CHECK((rg8[0] == 64 && (rg8[1] == 127 || rg8[1] == 128)));
        CHECK(Read<std::uint16_t>(render(Spot::TextureFormat::R16Float), 0) == ToHalf(0.25f));
        const auto rg16 = render(Spot::TextureFormat::Rg16Float);
        CHECK((Read<std::uint16_t>(rg16, 0) == ToHalf(0.25f) && Read<std::uint16_t>(rg16, 2) == ToHalf(0.5f)));
        CHECK(Read<float>(render(Spot::TextureFormat::R32Float), 0) == 0.25f);
        // Red in the low 10 bits: 0.25 of 1023, rounded.
        CHECK((Read<std::uint32_t>(render(Spot::TextureFormat::Rgb10A2), 0) & 0x3FF) == 256);
    });
}

TEST_CASE("Compressed textures take their mip levels one by one and are decoded as they are sampled", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        if (!device.GetCapabilities().compressedTextures) {
            SKIP("This GPU has no block-compressed textures");
        }
        CHECK(Spot::GetTextureDataSize(Spot::TextureFormat::Bc1, 8, 8) == 32);
        CHECK(Spot::GetTextureDataSize(Spot::TextureFormat::Bc7, 2, 1) == 16);
        CHECK_FALSE(device.SupportsRenderTarget(Spot::TextureFormat::Bc7));
        CHECK_FALSE(
            device.CreateTexture({.width = 4, .height = 4, .format = Spot::TextureFormat::Bc1, .renderTarget = true}));

        // BC1 blocks of one color: both endpoints the same RGB565 color, every pixel taking the first.
        const auto block = [](std::uint16_t color) {
            return std::array<std::uint8_t, 8>{static_cast<std::uint8_t>(color),
                                               static_cast<std::uint8_t>(color >> 8),
                                               static_cast<std::uint8_t>(color),
                                               static_cast<std::uint8_t>(color >> 8),
                                               0,
                                               0,
                                               0,
                                               0};
        };
        constexpr std::uint16_t Red = 0xF800;
        constexpr std::uint16_t Green = 0x07E0;
        auto texture =
            device.CreateTexture({.width = 8, .height = 8, .format = Spot::TextureFormat::Bc1, .mipmaps = true});
        REQUIRE(texture);
        REQUIRE(texture->GetMipLevelCount() == 4);
        std::vector<std::uint8_t> level0;
        for (int i = 0; i < 4; ++i) {
            const auto red = block(Red);
            level0.insert(level0.end(), red.begin(), red.end());
        }
        device.UploadToTextureLevel(*texture, 0, level0);
        for (std::uint32_t level = 1; level < 4; ++level) {
            device.UploadToTextureLevel(*texture, level, block(Green));
        }
        CHECK(device.DownloadTexture(*texture) == level0);
        const auto green = block(Green);
        CHECK(device.DownloadTexture(*texture, 3) == std::vector<std::uint8_t>(green.begin(), green.end()));

        // Drawn at 4x4 pixels, the 8x8 texture is minified onto its second level, green; its first is red.
        const auto pixels = DrawTexture(device, *texture);
        REQUIRE(pixels.size() == 4 * 4 * 4);
        CHECK((pixels[0] < 10 && pixels[1] > 245 && pixels[2] < 10));
    });
}

TEST_CASE("Devices tell what their backend can do", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        const Spot::GraphicsCapabilities& capabilities = device.GetCapabilities();
        CHECK(capabilities.maxColorTargets >= 1);
        CHECK(device.SupportsRenderTarget(Spot::TextureFormat::Rgba8));
        CHECK(device.SupportsSampling(Spot::TextureFormat::Rgba8));
    });
}
