// The graphics API's features beyond drawing, on every backend this machine runs: formats, compressed textures, and
// what GraphicsCapabilities tells apart.
#include "fill.comp.h"
#include "indirect_arguments.comp.h"
#include "solid.frag.h"
#include "texture_table.frag.h"
#include "textured.frag.h"
#include "textured_quad.vert.h"
#include "two_targets.frag.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

// A quad over the whole of its target, drawn with a fragment shader that reads the quad's UVs.
class Quad {
public:
    explicit Quad(Spot::GraphicsDevice& device) : m_device(device) {
        const std::array<Vertex, 4> vertices{{
            {{-1.0f, 1.0f}, {0.0f, 0.0f}},
            {{1.0f, 1.0f}, {1.0f, 0.0f}},
            {{1.0f, -1.0f}, {1.0f, 1.0f}},
            {{-1.0f, -1.0f}, {0.0f, 1.0f}},
        }};
        const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};
        m_vertices = device.CreateBuffer(Spot::BufferUsage::Vertex, vertices);
        m_indices = device.CreateBuffer(Spot::BufferUsage::Index, indices);
        m_vertexShader = device.CreateShader(Shaders::TexturedQuadVert);
        REQUIRE((m_vertices && m_indices && m_vertexShader));
    }

    std::unique_ptr<Spot::Pipeline> CreatePipeline(const Spot::Shader& fragmentShader,
                                                   std::vector<Spot::TextureFormat> additionalFormats = {},
                                                   bool textureTable = false) const {
        return m_device.CreatePipeline({
            .vertexShader = m_vertexShader.get(),
            .fragmentShader = &fragmentShader,
            .vertexBuffers = {{
                .stride = sizeof(Vertex),
                .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                               {1, Spot::VertexFormat::Float2, offsetof(Vertex, uv)}},
            }},
            .targetFormat = Spot::TextureFormat::Rgba8,
            .additionalTargetFormats = std::move(additionalFormats),
            .textureTable = textureTable,
        });
    }

    void Bind(Spot::RenderPass& pass, const Spot::Pipeline& pipeline) const {
        pass.BindPipeline(pipeline);
        pass.BindVertexBuffer(0, *m_vertices);
        pass.BindIndexBuffer(*m_indices, Spot::IndexType::UInt16);
    }

    void Draw(Spot::RenderPass& pass, const Spot::Pipeline& pipeline) const {
        Bind(pass, pipeline);
        pass.DrawIndexed(6);
    }

private:
    struct Vertex {
        Spot::Vector2 position;
        Spot::Vector2 uv;
    };

    Spot::GraphicsDevice& m_device;
    std::unique_ptr<Spot::Buffer> m_vertices;
    std::unique_ptr<Spot::Buffer> m_indices;
    std::unique_ptr<Spot::Shader> m_vertexShader;
};

// Draws the texture over the whole of a 4x4 RGBA8 target, and reads the target back.
std::vector<std::uint8_t> DrawTexture(Spot::GraphicsDevice& device, const Spot::Texture& texture) {
    const Quad quad(device);
    auto target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
    const auto sampler = device.CreateSampler({.filter = Spot::TextureFilter::Nearest});
    const auto fragmentShader = device.CreateShader(Shaders::TexturedFrag);
    REQUIRE((target && sampler && fragmentShader));
    const auto pipeline = quad.CreatePipeline(*fragmentShader);
    REQUIRE(pipeline);
    auto* context = device.BeginOffscreenFrame(*target);
    REQUIRE(context);
    {
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
        pass.BindFragmentTexture(0, texture, *sampler);
        pass.PushFragmentUniforms(0, Spot::Color::White);
        quad.Draw(pass, *pipeline);
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
            WARN("This GPU has no block-compressed textures");
            return;
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

TEST_CASE("Passes draw into several color targets at once", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        REQUIRE(device.GetCapabilities().maxColorTargets >= 2);
        const Quad quad(device);
        auto first = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        auto second = device.CreateTexture(
            {.width = 4, .height = 4, .format = Spot::TextureFormat::R16Float, .renderTarget = true});
        const auto shader = device.CreateShader(Shaders::TwoTargetsFrag);
        REQUIRE((first && second && shader));
        const auto pipeline = quad.CreatePipeline(*shader, {Spot::TextureFormat::R16Float});
        REQUIRE(pipeline);

        auto* context = device.BeginOffscreenFrame(*first);
        REQUIRE(context);
        {
            auto pass = context->BeginRenderPass(
                {.target = first.get(),
                 .clearColor = Spot::Color::Black,
                 .additionalTargets = {{.texture = second.get(), .clearColor = Spot::Color::Black}}});
            quad.Draw(pass, *pipeline);
        }
        device.EndFrame();
        const auto firstPixels = device.DownloadTexture(*first);
        const auto secondPixels = device.DownloadTexture(*second);
        REQUIRE(firstPixels.size() == 4 * 4 * 4);
        CHECK((firstPixels[0] == 255 && firstPixels[1] == 0));
        CHECK(Read<std::uint16_t>(secondPixels, 0) == ToHalf(0.75f));
    });
}

TEST_CASE("Compute passes write storage textures", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        if (!device.GetCapabilities().compute) {
            WARN("This device runs no compute shaders");
            return;
        }
        CHECK(Shaders::FillComp.threadCountX == 8);
        CHECK(Shaders::FillComp.readWriteStorageTextureCount == 1);
        CHECK(Shaders::IndirectArgumentsComp.storageBufferCount == 1);
        CHECK(Shaders::IndirectArgumentsComp.readWriteStorageBufferCount == 1);
        auto texture = device.CreateTexture({.width = 12, .height = 5, .storage = true});
        auto frame = device.CreateTexture({.width = 1, .height = 1, .renderTarget = true});
        const auto shader = device.CreateShader(Shaders::FillComp);
        REQUIRE((texture && frame && shader));
        const auto pipeline = device.CreateComputePipeline({.shader = shader.get()});
        REQUIRE(pipeline);

        auto* context = device.BeginOffscreenFrame(*frame);
        REQUIRE(context);
        {
            auto pass = context->BeginComputePass({.textures = {{.texture = texture.get()}}});
            pass.BindPipeline(*pipeline);
            pass.PushUniforms(0, Spot::Color{0.0f, 1.0f, 0.0f, 1.0f});
            pass.Dispatch(2, 1);
        }
        device.EndFrame();
        const auto pixels = device.DownloadTexture(*texture);
        REQUIRE(pixels.size() == 12 * 5 * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            REQUIRE((pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0 && pixels[i + 3] == 255));
        }
    });
}

TEST_CASE("Compute passes write the arguments of indirect dispatches and draws", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        if (!device.GetCapabilities().compute || !device.GetCapabilities().indirectDraws) {
            WARN("This device runs no compute shaders or indirect draws");
            return;
        }
        const Quad quad(device);
        auto target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        auto filled = device.CreateTexture({.width = 4, .height = 4, .storage = true});
        const auto counts = device.CreateBuffer(Spot::BufferUsage::Storage, std::array<std::uint32_t, 1>{6});
        auto arguments = device.CreateBuffer({.usage = Spot::BufferUsage::Indirect, .size = 8 * sizeof(std::uint32_t)});
        const auto argumentsShader = device.CreateShader(Shaders::IndirectArgumentsComp);
        const auto fillShader = device.CreateShader(Shaders::FillComp);
        const auto texturedShader = device.CreateShader(Shaders::TexturedFrag);
        const auto sampler = device.CreateSampler({.filter = Spot::TextureFilter::Nearest});
        REQUIRE(
            (target && filled && counts && arguments && argumentsShader && fillShader && texturedShader && sampler));
        const auto argumentsPipeline = device.CreateComputePipeline({.shader = argumentsShader.get()});
        const auto fillPipeline = device.CreateComputePipeline({.shader = fillShader.get()});
        const auto drawPipeline = quad.CreatePipeline(*texturedShader);
        REQUIRE((argumentsPipeline && fillPipeline && drawPipeline));

        auto* context = device.BeginOffscreenFrame(*target);
        REQUIRE(context);
        {
            auto pass = context->BeginComputePass({.buffers = {arguments.get()}});
            pass.BindPipeline(*argumentsPipeline);
            pass.BindStorageBuffer(0, *counts);
            pass.Dispatch(1);
        }
        {
            auto pass = context->BeginComputePass({.textures = {{.texture = filled.get()}}});
            pass.BindPipeline(*fillPipeline);
            pass.PushUniforms(0, Spot::Color{0.0f, 0.0f, 1.0f, 1.0f});
            pass.DispatchIndirect(*arguments, sizeof(Spot::DrawIndexedIndirectArguments));
        }
        {
            auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
            quad.Bind(pass, *drawPipeline);
            pass.BindFragmentTexture(0, *filled, *sampler);
            pass.PushFragmentUniforms(0, Spot::Color::White);
            pass.DrawIndexedIndirect(*arguments);
        }
        device.EndFrame();
        const auto pixels = device.DownloadTexture(*target);
        REQUIRE(pixels.size() == 4 * 4 * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            REQUIRE((pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 255 && pixels[i + 3] == 255));
        }
    });
}

TEST_CASE("Indexed indirect batches keep their vertex offsets and discarded arguments within a pass", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        const auto& capabilities = device.GetCapabilities();
        if (!capabilities.indirectDraws || capabilities.maxIndirectDrawCount == 0) {
            WARN("This device runs no indirect draws");
            return;
        }
        struct Vertex {
            Spot::Vector2 position;
            Spot::Vector2 uv;
        };
        // Two tiles across the top and one across the bottom, sharing one quad's indices.
        const std::array<Vertex, 12> vertices{{
            {{-1, 1}, {}},
            {{0, 1}, {}},
            {{0, 0}, {}},
            {{-1, 0}, {}},
            {{0, 1}, {}},
            {{1, 1}, {}},
            {{1, 0}, {}},
            {{0, 0}, {}},
            {{-1, 0}, {}},
            {{1, 0}, {}},
            {{1, -1}, {}},
            {{-1, -1}, {}},
        }};
        const auto vertexBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, vertices);
        const auto indexBuffer =
            device.CreateBuffer(Spot::BufferUsage::Index, std::array<std::uint32_t, 6>{0, 1, 2, 0, 2, 3});
        const auto vertexShader = device.CreateShader(Shaders::TexturedQuadVert);
        const auto fragmentShader = device.CreateShader(Shaders::SolidFrag);
        auto target = device.CreateTexture({.width = 8, .height = 8, .renderTarget = true});
        constexpr std::uint32_t stride = sizeof(Spot::DrawIndexedIndirectArguments);
        auto arguments = device.CreateBuffer({.usage = Spot::BufferUsage::Indirect, .size = 4 * stride});
        REQUIRE((vertexBuffer && indexBuffer && vertexShader && fragmentShader && target && arguments));
        const auto pipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .vertexBuffers = {{.stride = sizeof(Vertex),
                               .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                                              {1, Spot::VertexFormat::Float2, offsetof(Vertex, uv)}}}},
            .targetFormat = Spot::TextureFormat::Rgba8,
        });
        REQUIRE(pipeline);
        const std::array<Spot::DrawIndexedIndirectArguments, 3> top{{
            {},
            {.indexCount = 6, .instanceCount = 1},
            {.indexCount = 6, .instanceCount = 1, .vertexOffset = 4},
        }};
        const std::array<Spot::DrawIndexedIndirectArguments, 2> bottom{{
            {},
            {.indexCount = 6, .instanceCount = 1, .vertexOffset = 8},
        }};
        for (int frame = 0; frame < 6; ++frame) {
            auto* context = device.BeginOffscreenFrame(*target);
            REQUIRE(context);
            {
                auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
                pass.BindPipeline(*pipeline);
                pass.BindVertexBuffer(0, *vertexBuffer);
                pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt32);
                device.UploadToBuffer(*arguments, top, 0, Spot::UploadMode::Discard);
                pass.PushFragmentUniforms(0, Spot::Color::Red);
                for (std::uint32_t first = 0; first < 2;) {
                    const std::uint32_t count = std::min(2u - first, capabilities.maxIndirectDrawCount);
                    pass.DrawIndexedIndirect(*arguments, (first + 1) * stride, count);
                    first += count;
                }
                device.UploadToBuffer(*arguments, bottom, 0, Spot::UploadMode::Discard);
                pass.PushFragmentUniforms(0, Spot::Color::Green);
                pass.DrawIndexedIndirect(*arguments, stride);
            }
            device.EndFrame();
        }
        const auto pixels = device.DownloadTexture(*target);
        REQUIRE(pixels.size() == 8 * 8 * 4);
        for (std::uint32_t y = 0; y < 8; ++y) {
            for (std::uint32_t x = 0; x < 8; ++x) {
                const std::uint32_t at = (y * 8 + x) * 4;
                REQUIRE((pixels[at] == (y < 4 ? 255 : 0) && pixels[at + 1] == (y < 4 ? 0 : 255) &&
                         pixels[at + 2] == 0 && pixels[at + 3] == 255));
            }
        }
    });
}

TEST_CASE("GPU timers measure parts of frames once the GPU is done with them", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        auto target = device.CreateTexture({.width = 64, .height = 64, .renderTarget = true});
        REQUIRE(target);
        for (int frame = 0; frame < 4; ++frame) {
            auto* context = device.BeginOffscreenFrame(*target);
            REQUIRE(context);
            context->BeginGpuTimer("Frame");
            context->BeginGpuTimer("Clear");
            context->BeginRenderPass({.clearColor = Spot::Color::Red}).End();
            context->EndGpuTimer();
            context->EndGpuTimer();
            device.EndFrame();
        }
        const auto& timings = device.GetGpuTimings();
        if (!device.GetCapabilities().timestamps) {
            CHECK(timings.empty());
            return;
        }
        REQUIRE(timings.size() == 2);
        CHECK(timings[0].name == "Frame");
        CHECK(timings[0].depth == 0);
        CHECK(timings[1].name == "Clear");
        CHECK(timings[1].depth == 1);
        CHECK(timings[0].milliseconds >= timings[1].milliseconds);
        CHECK(timings[1].milliseconds >= 0.0);
        CHECK(timings[0].milliseconds < 1000.0);
    });
}

TEST_CASE("Texture tables let shaders pick textures by index", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        if (!device.GetCapabilities().bindlessTextures) {
            CHECK_FALSE(device.CreateTextureTable(4));
            WARN("This device has no texture tables");
            return;
        }
        const Quad quad(device);
        auto table = device.CreateTextureTable(8);
        REQUIRE(table);
        CHECK(table->GetCapacity() == 8);
        const std::array<std::array<std::uint8_t, 4>, 3> colors{{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}}};
        std::vector<std::unique_ptr<Spot::Texture>> textures;
        for (std::uint32_t i = 0; i < colors.size(); ++i) {
            auto texture = device.CreateTexture({.width = 1, .height = 1});
            REQUIRE(texture);
            device.UploadToTexture(*texture, colors[i]);
            device.SetTextureTableEntry(*table, i, *texture);
            textures.push_back(std::move(texture));
        }
        // The table keeps its textures alive.
        textures.clear();
        const auto shader = device.CreateShader(Shaders::TextureTableFrag);
        REQUIRE(shader);
        auto target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        REQUIRE(target);
        for (std::uint32_t pick = 0; pick < colors.size(); ++pick) {
            const auto drawPipeline = quad.CreatePipeline(*shader, {}, true);
            REQUIRE(drawPipeline);
            auto* context = device.BeginOffscreenFrame(*target);
            REQUIRE(context);
            {
                auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
                pass.BindTextureTable(*table);
                pass.PushFragmentUniforms(0, std::array<std::uint32_t, 4>{pick, 0, 0, 0});
                quad.Draw(pass, *drawPipeline);
            }
            device.EndFrame();
            const auto pixels = device.DownloadTexture(*target);
            REQUIRE(pixels.size() == 4 * 4 * 4);
            CHECK(std::array<std::uint8_t, 4>{pixels[0], pixels[1], pixels[2], pixels[3]} == colors[pick]);
        }
    });
}

TEST_CASE("Render graphs run their passes with the textures they keep", "[gpu][rhi]") {
    ForEachBackend([](Spot::GraphicsDevice& device) {
        const Quad quad(device);
        auto target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        const auto sampler = device.CreateSampler({.filter = Spot::TextureFilter::Nearest});
        const auto fragmentShader = device.CreateShader(Shaders::TexturedFrag);
        REQUIRE((target && sampler && fragmentShader));
        const auto pipeline = quad.CreatePipeline(*fragmentShader);
        REQUIRE(pipeline);

        Spot::RenderGraph graph;
        for (int frame = 0; frame < 2; ++frame) {
            auto* context = device.BeginOffscreenFrame(*target);
            REQUIRE(context);
            const auto output = graph.Import("Target", nullptr);
            Spot::RenderGraphTexture fill;
            bool culledRan = false;
            graph.AddPass(
                "Fill",
                [&](Spot::RenderGraphBuilder& pass) {
                    fill = pass.Create("Fill", {.width = 4, .height = 4, .renderTarget = true});
                    pass.Write(fill);
                },
                [&](Spot::RenderGraphResources& resources) {
                    REQUIRE(resources.GetTexture(fill));
                    resources.GetContext()
                        .BeginRenderPass({.target = resources.GetTexture(fill), .clearColor = Spot::Color::Green})
                        .End();
                });
            graph.AddPass(
                "Culled", [&](Spot::RenderGraphBuilder& pass) { pass.Read(fill); },
                [&](Spot::RenderGraphResources&) { culledRan = true; });
            graph.AddPass(
                "Show",
                [&](Spot::RenderGraphBuilder& pass) {
                    pass.Read(fill);
                    pass.Write(output);
                },
                [&](Spot::RenderGraphResources& resources) {
                    auto pass = resources.GetContext().BeginRenderPass(
                        {.target = resources.GetTexture(output), .clearColor = Spot::Color::Black});
                    pass.BindFragmentTexture(0, *resources.GetTexture(fill), *sampler);
                    pass.PushFragmentUniforms(0, Spot::Color::White);
                    quad.Draw(pass, *pipeline);
                });
            graph.Execute(device, *context);
            device.EndFrame();

            CHECK_FALSE(culledRan);
            CHECK(graph.GetStats().culledPasses == 1);
            // The second frame reuses the texture the first made.
            CHECK(graph.GetStats().createdTextures == (frame == 0 ? 1u : 0u));
            const auto pixels = device.DownloadTexture(*target);
            REQUIRE(pixels.size() == 4 * 4 * 4);
            CHECK((pixels[0] < 10 && pixels[1] > 245 && pixels[2] < 10));
        }
    });
}
