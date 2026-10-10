#include "depth_compare.frag.h"
#include "quad.vert.h"
#include "solid.frag.h"
#include "textured.frag.h"
#include "textured_quad.vert.h"
#include "triangle.frag.h"
#include "triangle.vert.h"
#include "uv.frag.h"

#include "rendering_application.h"
#include "temp_directory.h"

#include <spot.h>
#include <spot_import.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using Spot::Tests::RenderingApplication;
using Spot::Tests::RequireGpu;

TEST_CASE("Application renders frames sized to the window in pixels", "[gpu]") {
    RequireGpu();
    std::string backend;
    int width = 0;
    int pixelWidth = 0;

    bool linearWindow = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        backend = app.GetGraphicsDevice().GetBackendName();
        linearWindow = Spot::IsSrgbFormat(app.GetGraphicsDevice().GetWindowFormat());
        width = context.GetWidth();
        pixelWidth = app.GetWindow().GetPixelWidth();
        auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Blue});
        CHECK(pass.GetNativeHandle() != nullptr);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
    CHECK_FALSE(backend.empty());
    CHECK(width == pixelWidth);
    // Shaders write linear colors, which the window encodes for display.
    CHECK(linearWindow);
}

TEST_CASE("Windows can show what shaders write without encoding it", "[gpu]") {
    RequireGpu();
    bool linearWindow = true;

    RenderingApplication application(
        [&](Spot::Application& app, Spot::RenderContext& context) {
            linearWindow = Spot::IsSrgbFormat(app.GetGraphicsDevice().GetWindowFormat());
            auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Blue});
        },
        {}, {.linearWindow = false});

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
    CHECK_FALSE(linearWindow);
}

TEST_CASE("Render passes can clear or keep the target, one after the other", "[gpu]") {
    RequireGpu();

    RenderingApplication application([](Spot::Application&, Spot::RenderContext& context) {
        auto clear = context.BeginRenderPass({.clearColor = Spot::Color::Black});
        clear.End();
        clear.End();

        auto keep = context.BeginRenderPass();
        CHECK(keep.GetNativeHandle() != nullptr);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
}

TEST_CASE("VSync can be turned off and back on", "[gpu]") {
    RequireGpu();
    bool enabledAgain = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        auto& device = app.GetGraphicsDevice();
        device.SetVSync(false);
        device.SetVSync(true);
        enabledAgain = device.IsVSyncEnabled();
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(enabledAgain);
}

TEST_CASE("Shaders and pipelines draw a triangle", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Pipeline> pipeline;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        if (!pipeline) {
            auto& device = app.GetGraphicsDevice();
            const auto vertexShader = device.CreateShader(Shaders::TriangleVert);
            const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
            REQUIRE(vertexShader != nullptr);
            REQUIRE(fragmentShader != nullptr);
            CHECK(vertexShader->GetStage() == Spot::ShaderStage::Vertex);

            pipeline = device.CreatePipeline({.vertexShader = vertexShader.get(),
                                              .fragmentShader = fragmentShader.get(),
                                              .blendMode = Spot::BlendMode::Alpha});
            REQUIRE(pipeline != nullptr);
        }

        auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Black});
        pass.BindPipeline(*pipeline);
        pass.Draw(3);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
    // Released after the device: the pipeline keeps the backend alive until then.
    pipeline.reset();
}

TEST_CASE("Shaders without code for the backend are rejected", "[gpu]") {
    RequireGpu();
    bool rejected = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        rejected = app.GetGraphicsDevice().CreateShader({.stage = Spot::ShaderStage::Vertex}) == nullptr;
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(rejected);
}

TEST_CASE("Vertex and index buffers draw an indexed quad with uniforms", "[gpu]") {
    RequireGpu();

    struct Vertex {
        Spot::Vector2 position;
        Spot::Color color;
    };
    const std::array<Vertex, 4> vertices{{
        {{-0.5f, -0.5f}, Spot::Color::Red},
        {{0.5f, -0.5f}, Spot::Color::Green},
        {{0.5f, 0.5f}, Spot::Color::Blue},
        {{-0.5f, 0.5f}, Spot::Color::White},
    }};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};

    std::unique_ptr<Spot::Buffer> vertexBuffer;
    std::unique_ptr<Spot::Buffer> indexBuffer;
    std::unique_ptr<Spot::Pipeline> pipeline;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (!pipeline) {
            vertexBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, vertices);
            indexBuffer = device.CreateBuffer(Spot::BufferUsage::Index, indices);
            REQUIRE(vertexBuffer != nullptr);
            REQUIRE(indexBuffer != nullptr);
            CHECK(vertexBuffer->GetSize() == sizeof(vertices));
            CHECK(indexBuffer->GetUsage() == Spot::BufferUsage::Index);

            const auto vertexShader = device.CreateShader(Shaders::QuadVert);
            const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
            REQUIRE(vertexShader != nullptr);
            REQUIRE(fragmentShader != nullptr);
            pipeline = device.CreatePipeline({
                .vertexShader = vertexShader.get(),
                .fragmentShader = fragmentShader.get(),
                .vertexBuffers = {{
                    .stride = sizeof(Vertex),
                    .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                                   {1, Spot::VertexFormat::Float4, offsetof(Vertex, color)}},
                }},
            });
            REQUIRE(pipeline != nullptr);
        } else {
            // Partial updates during a frame, while earlier frames may still read the buffer.
            device.UploadToBuffer(*vertexBuffer, std::span(vertices).subspan(1, 2), sizeof(Vertex));
        }

        auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Black});
        pass.BindPipeline(*pipeline);
        pass.BindVertexBuffer(0, *vertexBuffer);
        pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
        pass.PushVertexUniforms(0, Spot::Matrix4::Scale({1.5f, 1.5f, 1.0f}));
        pass.DrawIndexed(static_cast<std::uint32_t>(indices.size()));
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
}

TEST_CASE("Empty buffers are rejected", "[gpu]") {
    RequireGpu();
    bool rejected = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        rejected = app.GetGraphicsDevice().CreateBuffer({.usage = Spot::BufferUsage::Vertex, .size = 0}) == nullptr;
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(rejected);
}

TEST_CASE("Textures round-trip their pixels", "[gpu]") {
    RequireGpu();
    const std::vector<std::uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    std::vector<std::uint8_t> downloaded;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        if (downloaded.empty()) {
            auto& device = app.GetGraphicsDevice();
            const auto texture = device.CreateTexture({.width = 2, .height = 2});
            REQUIRE(texture != nullptr);
            device.UploadToTexture(*texture, pixels);
            downloaded = device.DownloadTexture(*texture);
        }
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(downloaded == pixels);
}

TEST_CASE("Render passes draw sampled textures into render targets", "[gpu]") {
    RequireGpu();
    const auto image = Spot::Image::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "pixels.png");
    REQUIRE(image.has_value());

    struct Vertex {
        Spot::Vector2 position;
        Spot::Vector2 uv;
    };
    // A quad covering the whole target, with the image's top-left corner at the target's top-left.
    const std::array<Vertex, 4> vertices{{
        {{-1.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 1.0f}, {1.0f, 0.0f}},
        {{1.0f, -1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    }};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};

    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            // The first frame has been submitted by now, so its drawing can be read back.
            result = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }

        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        const auto texture = device.CreateTexture(*image);
        const auto sampler = device.CreateSampler({.filter = Spot::TextureFilter::Nearest});
        const auto vertexBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, vertices);
        const auto indexBuffer = device.CreateBuffer(Spot::BufferUsage::Index, indices);
        const auto vertexShader = device.CreateShader(Shaders::TexturedQuadVert);
        const auto fragmentShader = device.CreateShader(Shaders::TexturedFrag);
        REQUIRE((target && texture && sampler && vertexBuffer && indexBuffer && vertexShader && fragmentShader));
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
        REQUIRE(pipeline != nullptr);

        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        pass.BindPipeline(*pipeline);
        pass.BindVertexBuffer(0, *vertexBuffer);
        pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
        pass.BindFragmentTexture(0, *texture, *sampler);
        pass.PushFragmentUniforms(0, Spot::Color::White);
        pass.DrawIndexed(6);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(result.size() == 4 * 4 * 4);

    const auto pixelAt = [&](int x, int y) {
        const auto* pixel = &result[static_cast<std::size_t>((y * 4 + x) * 4)];
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    CHECK(pixelAt(0, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(pixelAt(3, 0) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
    CHECK(pixelAt(0, 3) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
    CHECK(pixelAt(3, 3) == std::array<std::uint8_t, 4>{255, 255, 255, 0});
}

TEST_CASE("Mipmapped textures get a level per halving and keep their full-size pixels", "[gpu]") {
    RequireGpu();
    std::vector<std::uint8_t> pixels(4 * 4 * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint8_t>(i);
    }
    std::uint32_t levels = 0;
    std::uint32_t oddLevels = 0;
    bool depthMipmapsRejected = false;
    std::vector<std::uint8_t> downloaded;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        if (!downloaded.empty()) {
            return;
        }
        auto& device = app.GetGraphicsDevice();
        const auto texture = device.CreateTexture({.width = 4, .height = 4, .mipmaps = true});
        const auto odd = device.CreateTexture({.width = 5, .height = 3, .mipmaps = true});
        REQUIRE((texture && odd));
        levels = texture->GetMipLevelCount();
        oddLevels = odd->GetMipLevelCount();
        depthMipmapsRejected =
            device.CreateTexture(
                {.width = 4, .height = 4, .format = Spot::TextureFormat::Depth32Float, .mipmaps = true}) == nullptr;

        device.UploadToTexture(*texture, pixels);
        downloaded = device.DownloadTexture(*texture);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(levels == 3);
    CHECK(oddLevels == 3);
    CHECK(depthMipmapsRejected);
    CHECK(downloaded == pixels);
}

TEST_CASE("Depth targets hide what is drawn behind, whatever the order", "[gpu]") {
    RequireGpu();

    struct Vertex {
        Spot::Vector2 position;
        Spot::Color color;
    };
    const std::array<Vertex, 8> vertices{{
        {{-0.5f, -0.5f}, Spot::Color::Red},
        {{0.5f, -0.5f}, Spot::Color::Red},
        {{0.5f, 0.5f}, Spot::Color::Red},
        {{-0.5f, 0.5f}, Spot::Color::Red},
        {{-0.5f, -0.5f}, Spot::Color::Green},
        {{0.5f, -0.5f}, Spot::Color::Green},
        {{0.5f, 0.5f}, Spot::Color::Green},
        {{-0.5f, 0.5f}, Spot::Color::Green},
    }};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};

    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            result = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }

        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        const auto depth = device.CreateTexture(
            {.width = 4, .height = 4, .format = Spot::TextureFormat::Depth32Float, .renderTarget = true});
        const auto vertexBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, vertices);
        const auto indexBuffer = device.CreateBuffer(Spot::BufferUsage::Index, indices);
        const auto vertexShader = device.CreateShader(Shaders::QuadVert);
        const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
        REQUIRE((target && depth && vertexBuffer && indexBuffer && vertexShader && fragmentShader));
        const auto pipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .vertexBuffers = {{
                .stride = sizeof(Vertex),
                .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                               {1, Spot::VertexFormat::Float4, offsetof(Vertex, color)}},
            }},
            .targetFormat = Spot::TextureFormat::Rgba8,
            .depth = Spot::DepthState{},
        });
        REQUIRE(pipeline != nullptr);

        auto pass = context.BeginRenderPass(
            {.target = target.get(), .clearColor = Spot::Color::Black, .depthTarget = depth.get(), .clearDepth = 1.0f});
        pass.BindPipeline(*pipeline);
        pass.BindVertexBuffer(0, *vertexBuffer);
        pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
        // The small red quad is near; the green one covering the target is far, and drawn last.
        pass.PushVertexUniforms(0, Spot::Matrix4::Translation({0.0f, 0.0f, 0.2f}));
        pass.DrawIndexed(6);
        pass.PushVertexUniforms(0, Spot::Matrix4::Translation({0.0f, 0.0f, 0.8f}) *
                                       Spot::Matrix4::Scale({2.0f, 2.0f, 1.0f}));
        pass.DrawIndexed(6, 1, 0, 4);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(result.size() == 4 * 4 * 4);

    const auto pixelAt = [&](int x, int y) {
        const auto* pixel = &result[static_cast<std::size_t>((y * 4 + x) * 4)];
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    CHECK(pixelAt(1, 1) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(pixelAt(2, 2) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(pixelAt(0, 0) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
    CHECK(pixelAt(3, 3) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
}

TEST_CASE("Depth-only passes draw depth into viewports, biased when asked, for comparison samplers", "[gpu]") {
    RequireGpu();

    struct Vertex {
        Spot::Vector2 position;
        Spot::Color color;
    };
    const std::array<Vertex, 4> quad{{
        {{-1.0f, -1.0f}, Spot::Color::White},
        {{1.0f, -1.0f}, Spot::Color::White},
        {{1.0f, 1.0f}, Spot::Color::White},
        {{-1.0f, 1.0f}, Spot::Color::White},
    }};
    struct UvVertex {
        Spot::Vector2 position;
        Spot::Vector2 uv;
    };
    const std::array<UvVertex, 4> uvQuad{{
        {{-1.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 1.0f}, {1.0f, 0.0f}},
        {{1.0f, -1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    }};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};

    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            result = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }

        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        const auto depth = device.CreateTexture(
            {.width = 4, .height = 4, .format = Spot::TextureFormat::Depth32Float, .renderTarget = true});
        const auto quadBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, quad);
        const auto uvQuadBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, uvQuad);
        const auto indexBuffer = device.CreateBuffer(Spot::BufferUsage::Index, indices);
        const auto quadShader = device.CreateShader(Shaders::QuadVert);
        const auto colorShader = device.CreateShader(Shaders::TriangleFrag);
        const auto uvQuadShader = device.CreateShader(Shaders::TexturedQuadVert);
        const auto compareShader = device.CreateShader(Shaders::DepthCompareFrag);
        const auto sampler =
            device.CreateSampler({.filter = Spot::TextureFilter::Nearest, .compare = Spot::CompareOp::LessOrEqual});
        REQUIRE((target && depth && quadBuffer && uvQuadBuffer && indexBuffer && quadShader && colorShader &&
                 uvQuadShader && compareShader && sampler));

        Spot::PipelineDesc depthDesc{
            .vertexShader = quadShader.get(),
            .fragmentShader = colorShader.get(),
            .vertexBuffers = {{
                .stride = sizeof(Vertex),
                .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                               {1, Spot::VertexFormat::Float4, offsetof(Vertex, color)}},
            }},
            .depthOnly = true,
            .depth = Spot::DepthState{.compare = Spot::CompareOp::Always},
        };
        const auto depthPipeline = device.CreatePipeline(depthDesc);
        // At a depth of 0.5, the smallest step of a 32-bit float is 2^-24, so this pushes depth back by about 0.006.
        depthDesc.depth->biasConstant = 100000.0f;
        const auto biasedPipeline = device.CreatePipeline(depthDesc);
        const auto comparePipeline = device.CreatePipeline({
            .vertexShader = uvQuadShader.get(),
            .fragmentShader = compareShader.get(),
            .vertexBuffers = {{
                .stride = sizeof(UvVertex),
                .attributes = {{0, Spot::VertexFormat::Float2, offsetof(UvVertex, position)},
                               {1, Spot::VertexFormat::Float2, offsetof(UvVertex, uv)}},
            }},
            .targetFormat = Spot::TextureFormat::Rgba8,
        });
        REQUIRE((depthPipeline && biasedPipeline && comparePipeline));

        // The top half is left at the cleared depth; the bottom-left quarter is drawn at 0.5 and the bottom-right one
        // at 0.5 biased. Drawing always wins the depth test, so a viewport that leaked would cover the whole target.
        {
            auto pass = context.BeginRenderPass({.depthTarget = depth.get(), .clearDepth = 1.0f, .depthOnly = true});
            pass.BindVertexBuffer(0, *quadBuffer);
            pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
            pass.PushVertexUniforms(0, Spot::Matrix4::Translation({0.0f, 0.0f, 0.5f}));
            pass.BindPipeline(*depthPipeline);
            pass.SetViewport({.x = 0.0f, .y = 2.0f, .width = 2.0f, .height = 2.0f});
            pass.DrawIndexed(6);
            pass.BindPipeline(*biasedPipeline);
            pass.SetViewport({.x = 2.0f, .y = 2.0f, .width = 2.0f, .height = 2.0f});
            pass.DrawIndexed(6);
        }

        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        pass.BindPipeline(*comparePipeline);
        pass.BindVertexBuffer(0, *uvQuadBuffer);
        pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
        pass.BindFragmentTexture(0, *depth, *sampler);
        pass.PushFragmentUniforms(0, std::array<float, 4>{0.503f, 0.0f, 0.0f, 0.0f});
        pass.DrawIndexed(6);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(result.size() == 4 * 4 * 4);

    const auto pixelAt = [&](int x, int y) {
        const auto* pixel = &result[static_cast<std::size_t>((y * 4 + x) * 4)];
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    constexpr std::array<std::uint8_t, 4> Passed{255, 255, 255, 255};
    constexpr std::array<std::uint8_t, 4> Failed{0, 0, 0, 255};
    CHECK(pixelAt(0, 0) == Passed);
    CHECK(pixelAt(3, 1) == Passed);
    CHECK(pixelAt(0, 2) == Failed);
    CHECK(pixelAt(1, 3) == Failed);
    CHECK(pixelAt(2, 2) == Passed);
    CHECK(pixelAt(3, 3) == Passed);
}

TEST_CASE("Scissor rectangles clip draws without moving them", "[gpu]") {
    RequireGpu();

    struct Vertex {
        Spot::Vector2 position;
        Spot::Color color;
    };
    const std::array<Vertex, 4> quad{{
        {{-1.0f, -1.0f}, Spot::Color::White},
        {{1.0f, -1.0f}, Spot::Color::White},
        {{1.0f, 1.0f}, Spot::Color::White},
        {{-1.0f, 1.0f}, Spot::Color::White},
    }};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};

    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            result = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }

        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        const auto quadBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, quad);
        const auto indexBuffer = device.CreateBuffer(Spot::BufferUsage::Index, indices);
        const auto vertexShader = device.CreateShader(Shaders::QuadVert);
        const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
        REQUIRE((target && quadBuffer && indexBuffer && vertexShader && fragmentShader));
        const auto pipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .vertexBuffers = {{
                .stride = sizeof(Vertex),
                .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                               {1, Spot::VertexFormat::Float4, offsetof(Vertex, color)}},
            }},
            .targetFormat = Spot::TextureFormat::Rgba8,
        });
        REQUIRE(pipeline != nullptr);

        // The quad covers the whole target, so only the scissor rectangle limits what it draws.
        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        pass.BindPipeline(*pipeline);
        pass.BindVertexBuffer(0, *quadBuffer);
        pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
        pass.PushVertexUniforms(0, Spot::Matrix4::Identity());
        pass.SetScissor({.x = 1, .y = 0, .width = 2, .height = 3});
        pass.DrawIndexed(6);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(result.size() == 4 * 4 * 4);

    const auto isDrawn = [&](int x, int y) { return result[static_cast<std::size_t>((y * 4 + x) * 4)] == 255; };
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            INFO("Pixel " << x << ", " << y);
            CHECK(isDrawn(x, y) == (x >= 1 && x < 3 && y < 3));
        }
    }
}

TEST_CASE("Renderer2D draws colored and textured quads in order", "[gpu]") {
    RequireGpu();
    const auto image = Spot::Image::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "pixels.png");
    REQUIRE(image.has_value());

    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    Spot::Renderer2DStats firstRound;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            result = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }

        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        const auto texture = device.CreateTexture(*image);
        REQUIRE((target && texture));
        Spot::Renderer2D renderer(device,
                                  {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
        REQUIRE(renderer.IsValid());

        // A 4x4 view: world X and Y both span [-2, 2], one unit per pixel.
        const Spot::Camera2D camera;
        const Spot::Vector2 view{4.0f, 4.0f};
        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});

        renderer.Begin(pass, camera, view);
        renderer.DrawQuad({-1.0f, 0.0f}, {2.0f, 4.0f}, Spot::Color::Red);
        renderer.DrawQuad({1.0f, 0.0f}, {2.0f, 4.0f}, *texture);
        renderer.End();
        firstRound = renderer.GetStats();

        // A second round in the same frame reuses the vertex buffer; the first round must keep its own data.
        renderer.Begin(pass, camera, view);
        renderer.DrawQuad({-1.5f, 1.5f}, {1.0f, 1.0f}, Spot::Color::Green);
        renderer.End();
        CHECK(renderer.GetStats().quadCount == 1);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(firstRound.quadCount == 2);
    CHECK(firstRound.drawCalls == 2);
    REQUIRE(result.size() == 4 * 4 * 4);

    const auto pixelAt = [&](int x, int y) {
        const auto* pixel = &result[static_cast<std::size_t>((y * 4 + x) * 4)];
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    CHECK(pixelAt(0, 0) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
    CHECK(pixelAt(1, 3) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(pixelAt(2, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(pixelAt(3, 0) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
    CHECK(pixelAt(2, 3) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
    // The texture's transparent texel blends to the black background.
    CHECK(pixelAt(3, 3) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
}

TEST_CASE("Renderer2D draws a scene's sprites in their order, through any camera", "[gpu]") {
    RequireGpu();
    const auto image = Spot::Image::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "pixels.png");
    REQUIRE(image.has_value());

    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    Spot::Renderer2DStats stats;
    Spot::Scene scene;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            result = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }
        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        std::shared_ptr<Spot::Texture> texture = device.CreateTexture(*image);
        REQUIRE((target && texture));

        // Red over everything, a green square under it, and the texture mirrored over the right half.
        auto& background = scene.CreateEntity("Background").AddComponent<Spot::SpriteRenderer>();
        background.SetSize({4.0f, 4.0f});
        background.SetColor(Spot::Color::Red);
        Spot::Entity& under = scene.CreateEntity("Under");
        under.GetTransform().SetPosition({-1.5f, 1.5f, 0.0f});
        auto& hidden = under.AddComponent<Spot::SpriteRenderer>();
        hidden.SetColor(Spot::Color::Green);
        hidden.SetOrder(-1);
        Spot::Entity& textured = scene.CreateEntity("Textured");
        textured.GetTransform().SetPosition({1.0f, 0.0f, 0.0f});
        textured.GetTransform().SetScale({2.0f, 4.0f, 1.0f});
        auto& mirrored = textured.AddComponent<Spot::SpriteRenderer>(Spot::AssetRef<Spot::Texture>(texture));
        mirrored.SetFlippedX(true);
        mirrored.SetOrder(1);
        Spot::Entity& inactive = scene.CreateEntity("Inactive");
        inactive.AddComponent<Spot::SpriteRenderer>().SetColor(Spot::Color::Blue);
        inactive.SetActive(false);

        Spot::Renderer2D renderer(device,
                                  {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
        REQUIRE(renderer.IsValid());
        // A 4x4 view of world X and Y from -2 to 2, through an orthographic Camera3D.
        Spot::Camera3D camera;
        camera.position = {0.0f, 0.0f, 10.0f};
        camera.orthographic = true;
        camera.orthographicSize = 2.0f;
        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        renderer.Begin(pass, camera.GetViewProjection({4.0f, 4.0f}));
        renderer.DrawScene(scene);
        renderer.End();
        stats = renderer.GetStats();
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(stats.quadCount == 3);
    REQUIRE(result.size() == 4 * 4 * 4);
    const auto pixelAt = [&](int x, int y) {
        const auto* pixel = &result[static_cast<std::size_t>((y * 4 + x) * 4)];
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    constexpr std::array<std::uint8_t, 4> Red{255, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> Green{0, 255, 0, 255};
    constexpr std::array<std::uint8_t, 4> Blue{0, 0, 255, 255};
    CHECK(pixelAt(0, 0) == Red);
    CHECK(pixelAt(0, 3) == Red);
    // Mirrored: the texture's left column, red and blue, is on the right.
    CHECK(pixelAt(2, 0) == Green);
    CHECK(pixelAt(3, 0) == Red);
    CHECK(pixelAt(3, 3) == Blue);
    // The texture's transparent texel shows the red under it.
    CHECK(pixelAt(2, 3) == Red);
}

TEST_CASE("Renderer2D draws text inside its measured box", "[gpu]") {
    RequireGpu();
    constexpr int Width = 128;
    constexpr int Height = 48;

    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    Spot::Vector2 measured;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            result = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }

        const auto font = Spot::Font::Load(
            device, std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "fonts" / "FiraSans-Regular.ttf", 32);
        target = device.CreateTexture({.width = Width, .height = Height, .renderTarget = true});
        REQUIRE((font && target));
        measured = font->MeasureText("Spot");

        Spot::Renderer2D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
        // Puts world (0, 0) at the target's top-left corner, so the text starts there.
        Spot::Camera2D camera;
        camera.position = {Width / 2.0f, -Height / 2.0f};

        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        renderer.Begin(pass, camera, {Width, Height});
        renderer.DrawString(*font, "Spot", {0.0f, 0.0f}, Spot::Color::White);
        renderer.End();
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(result.size() == static_cast<std::size_t>(Width * Height * 4));

    int litPixels = 0;
    int right = 0;
    int bottom = 0;
    for (int y = 0; y < Height; ++y) {
        for (int x = 0; x < Width; ++x) {
            if (result[static_cast<std::size_t>((y * Width + x) * 4)] > 64) {
                ++litPixels;
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }
    }
    CHECK(litPixels > 50);
    CHECK(static_cast<float>(right) <= measured.x + 1.0f);
    CHECK(static_cast<float>(bottom) <= measured.y + 1.0f);
}

namespace {

using Pixel = std::array<std::uint8_t, 4>;

// Renders one frame into a color and depth target of the given size and reads it back. With several samples, it draws
// into multisampled targets resolved into the color target. The draw function opens the pass described.
class TargetRender {
public:
    using Draw = std::function<void(Spot::GraphicsDevice&, Spot::RenderContext&, const Spot::RenderPassDesc& target)>;

    TargetRender(int width, int height, Draw draw, Spot::AssetConfig assets = {}, std::uint32_t sampleCount = 1)
        : m_width(width), m_height(height) {
        int frame = 0;
        std::unique_ptr<Spot::Texture> target;
        RenderingApplication application(
            [&](Spot::Application& app, Spot::RenderContext& context) {
                auto& device = app.GetGraphicsDevice();
                if (++frame == 2) {
                    m_pixels = device.DownloadTexture(*target);
                    return;
                }
                if (frame != 1) {
                    return;
                }

                const auto w = static_cast<std::uint32_t>(width);
                const auto h = static_cast<std::uint32_t>(height);
                target = device.CreateTexture({.width = w, .height = h, .renderTarget = true});
                const auto samples =
                    sampleCount > 1 ? device.CreateTexture(
                                          {.width = w, .height = h, .renderTarget = true, .sampleCount = sampleCount})
                                    : nullptr;
                const auto depth = device.CreateTexture({.width = w,
                                                         .height = h,
                                                         .format = Spot::TextureFormat::Depth32Float,
                                                         .renderTarget = true,
                                                         .sampleCount = sampleCount});
                REQUIRE((target && depth && (samples || sampleCount == 1)));
                draw(device, context,
                     {.target = samples ? samples.get() : target.get(),
                      .clearColor = Spot::Color::Black,
                      .depthTarget = depth.get(),
                      .clearDepth = 1.0f,
                      .resolveTarget = samples ? target.get() : nullptr});
            },
            std::move(assets));
        REQUIRE(application.Run() == EXIT_SUCCESS);
        REQUIRE(m_pixels.size() == static_cast<std::size_t>(width * height * 4));
    }

    Pixel At(int x, int y) const {
        const auto* pixel = &m_pixels[static_cast<std::size_t>((y * m_width + x) * 4)];
        return {pixel[0], pixel[1], pixel[2], pixel[3]};
    }

private:
    int m_width;
    int m_height;
    std::vector<std::uint8_t> m_pixels;
};

bool IsNear(Pixel a, Pixel b) {
    return std::ranges::all_of(std::array{0, 1, 2, 3}, [&](int i) { return std::abs(a[i] - b[i]) <= 2; });
}

// A camera three units in front of the origin, with a light shining straight away from it and no ambient light, so
// surfaces facing the camera show their exact base color.
Spot::Camera3D MakeFrontCamera() {
    Spot::Camera3D camera;
    camera.position = {0.0f, 0.0f, 3.0f};
    camera.LookAt({0.0f, 0.0f, 0.0f});
    return camera;
}

constexpr Spot::LightData FrontLight{.type = Spot::LightType::Directional, .direction = {0.0f, 0.0f, -1.0f}};

} // namespace

TEST_CASE("Renderer3D draws lit meshes with depth testing", "[gpu]") {
    RequireGpu();
    Spot::Renderer3DStats stats;

    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            REQUIRE(renderer.IsValid());
            const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            REQUIRE(cube != nullptr);

            // The large far cube is drawn last but stays behind the small near one.
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Green});
            renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Red},
                              Spot::Matrix4::Translation({0.0f, 0.0f, -2.0f}) *
                                  Spot::Matrix4::Scale({3.0f, 3.0f, 3.0f}));
            renderer.End();
            stats = renderer.GetStats();
        });

    CHECK(stats.drawCalls == 2);
    CHECK(stats.triangleCount == 24);
    CHECK(IsNear(render.At(16, 16), {0, 255, 0, 255}));
    CHECK(IsNear(render.At(16, 6), {255, 0, 0, 255}));
    CHECK(IsNear(render.At(0, 0), {0, 0, 0, 255}));
}

TEST_CASE("Renderer3D hides back faces unless the material is double-sided", "[gpu]") {
    RequireGpu();

    for (const bool doubleSided : {false, true}) {
        const TargetRender render(
            16, 16,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const auto plane = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(2.0f));
                REQUIRE(plane != nullptr);

                // The plane faces +Y; a quarter turn around X makes it face away from the camera.
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(FrontLight);
                renderer.DrawMesh(
                    *plane, {.baseColor = Spot::Color::Blue, .doubleSided = doubleSided},
                    Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({1, 0, 0}, -Spot::Pi / 2.0f)));
                renderer.End();
            });

        const Pixel expected = doubleSided ? Pixel{0, 0, 255, 255} : Pixel{0, 0, 0, 255};
        CHECK(IsNear(render.At(8, 8), expected));
    }
}

TEST_CASE("Renderer3D projects textures from world space when asked", "[gpu]") {
    RequireGpu();

    // A plane two units wide facing the camera, with a texture white on its left half and black on its right. Its
    // UVs stretch the texture across it once, while world projection repeats it every unit, so a quarter unit right of
    // the center is black with the UVs and white with the projection. A unit is about 18.5 pixels here.
    for (const bool projected : {false, true}) {
        const TargetRender render(
            64, 64,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(
                    device, {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
                const auto plane = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane());
                const std::shared_ptr<Spot::Texture> texture =
                    device.CreateTexture(Spot::Image(2, 1, {255, 255, 255, 255, 0, 0, 0, 255}));
                REQUIRE((plane && texture));

                const Spot::Material material{.baseColorTexture = Spot::AssetRef<Spot::Texture>(texture),
                                              .worldTextureSize = projected ? 1.0f : 0.0f};
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(FrontLight);
                renderer.DrawMesh(*plane, material,
                                  Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({1, 0, 0}, Spot::Pi / 2.0f)) *
                                      Spot::Matrix4::Scale({2.0f, 1.0f, 2.0f}));
                renderer.End();
            });

        const Pixel white{255, 255, 255, 255};
        const Pixel black{0, 0, 0, 255};
        CHECK(IsNear(render.At(27, 32), projected ? black : white));
        CHECK(IsNear(render.At(37, 32), projected ? white : black));
    }
}

TEST_CASE("Renderer3D samples mipmaps, anisotropically when asked", "[gpu]") {
    RequireGpu();

    // A floor a quarter unit below the camera, with stripes half a unit wide running away from it. Row 34 sees it six
    // units away, squeezed about twenty times more in depth than across, so mipmapping blurs the stripes to gray
    // where the full-size texture would alias, while anisotropic filtering can keep them apart.
    const auto contrastFar = [](float maxAnisotropy) {
        const TargetRender render(
            64, 64,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device,
                                          {.maxAnisotropy = maxAnisotropy, .targetFormat = Spot::TextureFormat::Rgba8});
                const auto plane = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(100.0f));
                std::vector<std::uint8_t> pixels(64 * 64 * 4);
                for (std::size_t i = 0; i < 64 * 64; ++i) {
                    const auto value = static_cast<std::uint8_t>(i % 16 < 8 ? 255 : 0);
                    pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = value;
                    pixels[i * 4 + 3] = 255;
                }
                const std::shared_ptr<Spot::Texture> texture =
                    device.CreateTexture(Spot::Image(64, 64, std::move(pixels)), true);
                REQUIRE((plane && texture));

                Spot::Camera3D camera;
                camera.position = {0.0f, 0.25f, 0.0f};
                renderer.Begin(context, target, camera);
                renderer.DrawLight({.type = Spot::LightType::Directional, .direction = {0.0f, -1.0f, 0.0f}});
                renderer.DrawMesh(
                    *plane, {.baseColorTexture = Spot::AssetRef<Spot::Texture>(texture), .worldTextureSize = 4.0f});
                renderer.End();
            });

        int darkest = 255;
        int brightest = 0;
        for (int x = 16; x < 48; ++x) {
            darkest = std::min<int>(darkest, render.At(x, 34)[0]);
            brightest = std::max<int>(brightest, render.At(x, 34)[0]);
        }
        return brightest - darkest;
    };

    const int plain = contrastFar(1.0f);
    const int anisotropic = contrastFar(16.0f);
    INFO("Contrast " << plain << " without anisotropic filtering and " << anisotropic << " with it");
    CHECK(plain < 32);
    // Drivers may ignore or limit anisotropy, as NVIDIA's Direct3D 12 driver did on the machine this was written on,
    // so it is only required not to make things worse.
    CHECK(anisotropic >= plain);
}

TEST_CASE("Multisampled targets smooth the edges they resolve", "[gpu]") {
    RequireGpu();

    // A green square turned a little, whose edges cross pixels: with one sample each pixel is either green or black,
    // while four samples give the pixels along the edges shades in between.
    const auto countEdgeShades = [](std::uint32_t sampleCount) {
        const TargetRender render(
            32, 32,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device,
                                          {.targetFormat = Spot::TextureFormat::Rgba8, .sampleCount = sampleCount});
                REQUIRE(renderer.IsValid());
                const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
                REQUIRE(cube != nullptr);
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(FrontLight);
                renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Green},
                                  Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({0, 0, 1}, 0.3f)));
                renderer.End();
            },
            {}, sampleCount);

        int shades = 0;
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const int green = render.At(x, y)[1];
                shades += green > 16 && green < 240 ? 1 : 0;
            }
        }
        return shades;
    };

    CHECK(countEdgeShades(1) == 0);
    CHECK(countEdgeShades(4) > 10);
}

TEST_CASE("Multisampled targets keep their samples for a later pass when asked", "[gpu]") {
    RequireGpu();
    struct Vertex {
        Spot::Vector2 position;
        Spot::Color color;
    };
    // The right half of the target.
    const std::array<Vertex, 4> quad{{
        {{0.0f, -1.0f}, Spot::Color::Green},
        {{1.0f, -1.0f}, Spot::Color::Green},
        {{1.0f, 1.0f}, Spot::Color::Green},
        {{0.0f, 1.0f}, Spot::Color::Green},
    }};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};
    std::unique_ptr<Spot::Texture> resolved;
    std::vector<std::uint8_t> result;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            result = device.DownloadTexture(*resolved);
            return;
        }
        if (frame != 1) {
            return;
        }
        REQUIRE(device.SupportsSampleCount(Spot::TextureFormat::Rgba8, 4));
        resolved = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        const auto samples = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true, .sampleCount = 4});
        const auto quadBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, quad);
        const auto indexBuffer = device.CreateBuffer(Spot::BufferUsage::Index, indices);
        const auto vertexShader = device.CreateShader(Shaders::QuadVert);
        const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
        REQUIRE((resolved && samples && quadBuffer && indexBuffer && vertexShader && fragmentShader));
        const auto pipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .vertexBuffers = {{
                .stride = sizeof(Vertex),
                .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                               {1, Spot::VertexFormat::Float4, offsetof(Vertex, color)}},
            }},
            .targetFormat = Spot::TextureFormat::Rgba8,
            .sampleCount = 4,
        });
        REQUIRE(pipeline != nullptr);

        {
            auto pass = context.BeginRenderPass({.target = samples.get(),
                                                 .clearColor = Spot::Color::Blue,
                                                 .resolveTarget = resolved.get(),
                                                 .keepSamples = true});
        }
        auto pass = context.BeginRenderPass({.target = samples.get(), .resolveTarget = resolved.get()});
        pass.BindPipeline(*pipeline);
        pass.BindVertexBuffer(0, *quadBuffer);
        pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
        pass.PushVertexUniforms(0, Spot::Matrix4::Identity());
        pass.DrawIndexed(6);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(result.size() == 4 * 4 * 4);
    const auto pixelAt = [&](int x, int y) {
        const auto* pixel = &result[static_cast<std::size_t>((y * 4 + x) * 4)];
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    // The first pass's clear survives on the left, under the second pass's draw on the right.
    CHECK(pixelAt(0, 1) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
    CHECK(pixelAt(3, 1) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
}

TEST_CASE("Multisampled targets resolve into the window", "[gpu]") {
    RequireGpu();
    bool resolved = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        const auto format = device.GetWindowFormat();
        REQUIRE(device.SupportsSampleCount(format, 4));
        const auto samples = device.CreateTexture({.width = static_cast<std::uint32_t>(context.GetWidth()),
                                                   .height = static_cast<std::uint32_t>(context.GetHeight()),
                                                   .format = format,
                                                   .renderTarget = true,
                                                   .sampleCount = 4});
        REQUIRE(samples != nullptr);
        auto pass = context.BeginRenderPass(
            {.target = samples.get(), .clearColor = Spot::Color::Blue, .resolveToWindow = true});
        resolved = pass.GetNativeHandle() != nullptr;
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(resolved);
}

TEST_CASE("Textures and pipelines reject sample counts they cannot have", "[gpu]") {
    RequireGpu();

    RenderingApplication application([](Spot::Application& app, Spot::RenderContext&) {
        auto& device = app.GetGraphicsDevice();
        CHECK(device.SupportsSampleCount(Spot::TextureFormat::Rgba8, 1));
        CHECK_FALSE(device.SupportsSampleCount(Spot::TextureFormat::Rgba8, 3));

        CHECK_FALSE(device.CreateTexture({.width = 4, .height = 4, .renderTarget = true, .sampleCount = 3}));
        // Multisampled textures can only be drawn into, then resolved.
        CHECK_FALSE(device.CreateTexture({.width = 4, .height = 4, .sampleCount = 4}));
        CHECK_FALSE(
            device.CreateTexture({.width = 4, .height = 4, .renderTarget = true, .mipmaps = true, .sampleCount = 4}));
        const auto samples = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true, .sampleCount = 4});
        REQUIRE(samples != nullptr);
        CHECK(samples->GetSampleCount() == 4);

        const auto vertexShader = device.CreateShader(Shaders::TriangleVert);
        const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
        REQUIRE((vertexShader && fragmentShader));
        CHECK_FALSE(device.CreatePipeline({.vertexShader = vertexShader.get(),
                                           .fragmentShader = fragmentShader.get(),
                                           .targetFormat = Spot::TextureFormat::Rgba8,
                                           .sampleCount = 5}));
        CHECK(device.CreatePipeline({.vertexShader = vertexShader.get(),
                                     .fragmentShader = fragmentShader.get(),
                                     .targetFormat = Spot::TextureFormat::Rgba8,
                                     .sampleCount = 4}));
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
}

TEST_CASE("Models load with their materials and draw textured", "[gpu]") {
    RequireGpu();
    const auto modelPath = std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models" / "textured_quad.gltf";
    std::size_t parts = 0;
    std::uint32_t mipLevels = 0;

    // The quad spans [-2, 2] at z = -2, which covers pixels 5 to 26 from z = 3. Its texture's red, green and blue
    // texels land in its top-left, top-right and bottom-left quarters.
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            auto model = Spot::Model::Load(device, modelPath);
            REQUIRE(model != nullptr);
            parts = model->GetParts().size();
            REQUIRE(model->GetParts().size() == 1);
            auto& part = model->GetParts()[0];
            REQUIRE(part.material);
            REQUIRE(part.material->baseColorTexture);
            mipLevels = part.material->baseColorTexture->GetMipLevelCount();
            // Replaces the file's tint, so the texels come out unchanged.
            Spot::Material untinted = *part.material;
            untinted.baseColor = Spot::Color::White;
            part.material = std::make_shared<const Spot::Material>(untinted);

            Spot::Renderer3D renderer(
                device, {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawModel(*model);
            renderer.End();
        });

    CHECK(parts == 1);
    CHECK(mipLevels == 2);
    CHECK(IsNear(render.At(8, 8), {255, 0, 0, 255}));
    CHECK(IsNear(render.At(23, 8), {0, 255, 0, 255}));
    CHECK(IsNear(render.At(8, 23), {0, 0, 255, 255}));
}

TEST_CASE("Renderer3D draws the active mesh renderers of a scene at their world transforms", "[gpu]") {
    RequireGpu();
    Spot::Renderer3DStats stats;

    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const std::shared_ptr<const Spot::Mesh> cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            REQUIRE(cube != nullptr);

            Spot::Scene scene;
            scene.CreateEntity("Near").AddComponent<Spot::MeshRenderer>(
                cube, Spot::Material{.baseColor = Spot::Color::Green});

            // The far cube gets its place and size from its parent.
            auto& parent = scene.CreateEntity("Parent");
            parent.GetTransform().SetPosition({0.0f, 0.0f, -2.0f});
            parent.GetTransform().SetScale({3.0f, 3.0f, 3.0f});
            parent.CreateChild("Far").AddComponent<Spot::MeshRenderer>(cube,
                                                                       Spot::Material{.baseColor = Spot::Color::Red});

            // None of these would let the green cube show, were they drawn.
            const Spot::Material blue{.baseColor = Spot::Color::Blue};
            auto& inactive = scene.CreateEntity("Inactive");
            inactive.GetTransform().SetPosition({0.0f, 0.0f, 1.0f});
            inactive.AddComponent<Spot::MeshRenderer>(cube, blue);
            inactive.SetActive(false);
            auto& disabled = scene.CreateEntity("Disabled");
            disabled.GetTransform().SetPosition({0.0f, 0.0f, 1.0f});
            disabled.AddComponent<Spot::MeshRenderer>(cube, blue).SetEnabled(false);
            scene.CreateEntity("Empty").AddComponent<Spot::MeshRenderer>();

            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawScene(scene);
            renderer.End();
            stats = renderer.GetStats();
        });

    CHECK(stats.drawCalls == 2);
    CHECK(IsNear(render.At(16, 16), {0, 255, 0, 255}));
    CHECK(IsNear(render.At(16, 6), {255, 0, 0, 255}));
    CHECK(IsNear(render.At(0, 0), {0, 0, 0, 255}));
}

namespace {

// A wall facing the front camera, its surface half a unit in front of the origin and wider than the view, lit only by
// the lights drawn.
TargetRender RenderLitWall(const std::function<void(Spot::Renderer3D&)>& drawLights,
                           const Spot::Material& material = {}) {
    return TargetRender(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            REQUIRE(cube != nullptr);
            renderer.Begin(context, target, MakeFrontCamera());
            drawLights(renderer);
            renderer.DrawMesh(*cube, material, Spot::Matrix4::Scale({20.0f, 20.0f, 1.0f}));
            renderer.End();
        });
}

} // namespace

TEST_CASE("Renderer3D lights surfaces only with the lights drawn", "[gpu]") {
    RequireGpu();

    SECTION("Without lights, surfaces are black") {
        const auto render = RenderLitWall([](Spot::Renderer3D&) {});
        CHECK(IsNear(render.At(16, 16), {0, 0, 0, 255}));
    }

    SECTION("Ambient lights add up and reach every surface evenly") {
        const auto render = RenderLitWall([](Spot::Renderer3D& renderer) {
            renderer.DrawLight({.type = Spot::LightType::Ambient, .color = Spot::Color::Red, .intensity = 0.25f});
            renderer.DrawLight({.type = Spot::LightType::Ambient, .color = Spot::Color::Red, .intensity = 0.25f});
        });
        CHECK(IsNear(render.At(16, 16), {128, 0, 0, 255}));
        CHECK(IsNear(render.At(0, 0), {128, 0, 0, 255}));
    }

    SECTION("Directional lights add up") {
        const auto render = RenderLitWall([](Spot::Renderer3D& renderer) {
            renderer.DrawLight(
                {.type = Spot::LightType::Directional, .color = Spot::Color::Red, .direction = {0, 0, -1}});
            renderer.DrawLight(
                {.type = Spot::LightType::Directional, .color = Spot::Color::Green, .direction = {0, 0, -1}});
        });
        CHECK(IsNear(render.At(16, 16), {255, 255, 0, 255}));
    }
}

TEST_CASE("Renderer3D point lights fade out by their range", "[gpu]") {
    RequireGpu();

    // One unit from the wall, so the edges of the view, over 1.4 units aside, are out of range.
    const auto render = RenderLitWall([](Spot::Renderer3D& renderer) {
        renderer.DrawLight({.type = Spot::LightType::Point, .position = {0.0f, 0.0f, 1.5f}, .range = 1.5f});
    });

    const int center = render.At(16, 16)[0];
    const int aside = render.At(10, 16)[0];
    INFO("Center " << center << ", aside " << aside);
    CHECK(center > 60);
    CHECK(aside > 0);
    CHECK(aside < center);
    CHECK(IsNear(render.At(0, 16), {0, 0, 0, 255}));
}

TEST_CASE("Renderer3D spot lights light only inside their cone", "[gpu]") {
    RequireGpu();

    const auto render = RenderLitWall([](Spot::Renderer3D& renderer) {
        renderer.DrawLight({.type = Spot::LightType::Spot,
                            .position = {0.0f, 0.0f, 1.5f},
                            .direction = {0.0f, 0.0f, -1.0f},
                            .innerConeAngle = Spot::ToRadians(10.0f),
                            .outerConeAngle = Spot::ToRadians(20.0f)});
    });

    CHECK(render.At(16, 16)[0] > 240);
    CHECK(IsNear(render.At(24, 16), {0, 0, 0, 255}));
}

TEST_CASE("Renderer3D adds highlights where specular materials reflect lights toward the camera", "[gpu]") {
    RequireGpu();

    // The light comes from behind the camera, so it reflects straight back at the center of the view only.
    const auto drawLight = [](Spot::Renderer3D& renderer) { renderer.DrawLight(FrontLight); };
    const auto matte = RenderLitWall(drawLight, {.baseColor = Spot::Color::Red});
    const auto glossy =
        RenderLitWall(drawLight, {.baseColor = Spot::Color::Red, .specular = 1.0f, .shininess = 256.0f});

    CHECK(IsNear(matte.At(16, 16), {255, 0, 0, 255}));
    // Highlights take the light's color, not the surface's. The pixel is a little off the exact center, where the
    // sharp highlight peaks.
    const Pixel highlight = glossy.At(16, 16);
    INFO("Highlight " << int{highlight[0]} << ", " << int{highlight[1]} << ", " << int{highlight[2]});
    CHECK(highlight[0] == 255);
    CHECK(highlight[1] > 200);
    CHECK(highlight[1] == highlight[2]);
    CHECK(IsNear(glossy.At(0, 16), {255, 0, 0, 255}));
}

TEST_CASE("Renderer3D keeps the lights nearest the camera when given too many", "[gpu]") {
    RequireGpu();

    // Far lights out of reach of the wall come first, but the near one drawn last is the one kept.
    const auto render = RenderLitWall([](Spot::Renderer3D& renderer) {
        for (std::size_t i = 0; i < Spot::Renderer3D::MaxLights; ++i) {
            renderer.DrawLight(
                {.type = Spot::LightType::Point, .color = Spot::Color::Red, .position = {0.0f, 0.0f, 50.0f}});
        }
        renderer.DrawLight({.type = Spot::LightType::Spot,
                            .color = Spot::Color::Green,
                            .position = {0.0f, 0.0f, 1.5f},
                            .direction = {0.0f, 0.0f, -1.0f}});
    });

    CHECK(render.At(16, 16)[1] > 240);
}

TEST_CASE("Images become sRGB textures that shaders read as linear colors", "[gpu]") {
    RequireGpu();

    // Mid-gray as stored in an image is about a fifth of white's light, so a white light shows it at that value in a
    // target that stores values as they are.
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            const std::shared_ptr<Spot::Texture> gray =
                device.CreateTexture(Spot::Image(1, 1, std::vector<std::uint8_t>{128, 128, 128, 255}));
            REQUIRE((cube && gray));
            CHECK(gray->GetFormat() == Spot::TextureFormat::Rgba8Srgb);
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawMesh(*cube, {.baseColorTexture = Spot::AssetRef<Spot::Texture>(gray)});
            renderer.End();
        });

    CHECK(IsNear(render.At(16, 16), {55, 55, 55, 255}));
}

TEST_CASE("Renderer3D lights scenes with their active lights", "[gpu]") {
    RequireGpu();

    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const std::shared_ptr<const Spot::Mesh> cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            REQUIRE(cube != nullptr);

            Spot::Scene scene;
            scene.CreateEntity("Wall").AddComponent<Spot::MeshRenderer>(cube);
            // With no rotation, it shines down -Z, straight onto the wall's front.
            scene.CreateEntity("Sun").AddComponent<Spot::DirectionalLight>(Spot::Color::Green);
            scene.CreateEntity("Disabled").AddComponent<Spot::AmbientLight>(Spot::Color::Red, 1.0f).SetEnabled(false);
            auto& inactive = scene.CreateEntity("Inactive");
            inactive.AddComponent<Spot::AmbientLight>(Spot::Color::Blue, 1.0f);
            inactive.SetActive(false);

            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawScene(scene);
            renderer.End();
        });

    CHECK(IsNear(render.At(16, 16), {0, 255, 0, 255}));
}

namespace {

// The sky seen by the front camera, which looks along the horizon: the top rows are sky and the bottom ones ground.
TargetRender RenderSky(const Spot::SkyData& sky, const Spot::Camera3D& camera = MakeFrontCamera()) {
    return TargetRender(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            REQUIRE(renderer.IsValid());
            renderer.Begin(context, target, camera);
            renderer.DrawSky(sky);
            renderer.End();
        });
}

} // namespace

TEST_CASE("Renderer3D draws a blue sky over the ground's color", "[gpu]") {
    RequireGpu();

    const auto render = RenderSky({.groundColor = Spot::Color::Red});
    const Pixel sky = render.At(16, 2);
    const Pixel ground = render.At(16, 29);
    INFO("Sky " << int{sky[0]} << ", " << int{sky[1]} << ", " << int{sky[2]} << "; ground " << int{ground[0]} << ", "
                << int{ground[1]} << ", " << int{ground[2]});
    CHECK(sky[2] > sky[0] + 40);
    CHECK(sky[2] > 120);
    CHECK(ground[0] > ground[2] + 40);
}

TEST_CASE("Renderer3D draws the sky behind meshes", "[gpu]") {
    RequireGpu();

    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            REQUIRE(cube != nullptr);
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Green});
            // Drawn last, yet behind. It does not light the cube, which shows the front light alone.
            renderer.DrawSky({.lightsScene = false});
            renderer.End();
        });

    CHECK(IsNear(render.At(16, 16), {0, 255, 0, 255}));
    CHECK(render.At(16, 2)[2] > 120);
}

TEST_CASE("Renderer3D skies redden as the sun nears the horizon and darken without it", "[gpu]") {
    RequireGpu();

    // The sun straight ahead, high and then low. Sunlight crossing more air loses more of its blue.
    const auto redness = [](const TargetRender& render) {
        const Pixel pixel = render.At(16, 14);
        return static_cast<float>(pixel[0]) / static_cast<float>(std::max<int>(pixel[2], 1));
    };
    const auto high = RenderSky({.sunDirection = {0.0f, -1.0f, 1.0f}, .sunSize = 0.0f});
    const auto low = RenderSky({.sunDirection = {0.0f, -0.03f, 1.0f}, .sunSize = 0.0f});
    INFO("Redness " << redness(high) << " with the sun high and " << redness(low) << " with it low");
    CHECK(redness(low) > redness(high) * 1.5f);

    const auto night = RenderSky({.hasSun = false});
    for (const Pixel pixel : {night.At(16, 2), night.At(16, 29)}) {
        CHECK(std::max({pixel[0], pixel[1], pixel[2]}) < 50);
    }
}

TEST_CASE("Renderer3D skies light the scene with a sun that sets on its own", "[gpu]") {
    RequireGpu();

    // The sun behind the camera lights the wall's front, until it sets. Only the sun lights it here.
    const auto renderWall = [](Spot::Vector3 sunDirection) {
        return RenderLitWall([=](Spot::Renderer3D& renderer) {
            renderer.DrawSky(
                {.sunDirection = sunDirection, .nightColor = Spot::Color::Black, .ambientIntensity = 0.0f});
        });
    };
    const int high = renderWall({0.0f, -1.0f, -1.0f}).At(16, 16)[0];
    const int low = renderWall({0.0f, -0.03f, -1.0f}).At(16, 16)[0];
    const auto set = renderWall({0.0f, 0.1f, -1.0f});
    INFO("Wall " << high << " with the sun high and " << low << " with it low");
    CHECK(high > 120);
    CHECK(low < high);
    CHECK(IsNear(set.At(16, 16), {0, 0, 0, 255}));

    // The sky's own light reaches what the sun does not.
    const auto skylit = RenderLitWall([](Spot::Renderer3D& renderer) {
        renderer.DrawSky({.sunDirection = {0.0f, -1.0f, 1.0f}, .nightColor = Spot::Color::Black});
    });
    CHECK(skylit.At(16, 16)[2] > skylit.At(16, 16)[0]);
    CHECK(skylit.At(16, 16)[2] > 10);
}

TEST_CASE("Renderer3D lights scenes with the sun of their sky through its air", "[gpu]") {
    RequireGpu();

    // A sun below the horizon would light the wall's front on its own, but the sky hides it.
    const auto render = [](float sunHeight) {
        return TargetRender(
            32, 32,
            [=](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const std::shared_ptr<const Spot::Mesh> cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
                REQUIRE(cube != nullptr);

                Spot::Scene scene;
                auto& wall = scene.CreateEntity("Wall");
                wall.GetTransform().SetScale({20.0f, 20.0f, 1.0f});
                wall.AddComponent<Spot::MeshRenderer>(cube);
                auto& sun = scene.CreateEntity("Sun");
                sun.GetTransform().SetRotation(Spot::Quaternion::LookRotation({0.0f, -sunHeight, -1.0f}));
                sun.AddComponent<Spot::DirectionalLight>();
                auto& sky = scene.CreateEntity("Sky").AddComponent<Spot::Sky>();
                sky.SetAmbientIntensity(0.0f);
                sky.SetNightColor(Spot::Color::Black);

                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawScene(scene);
                renderer.End();
            });
    };

    CHECK(render(1.0f).At(16, 16)[0] > 120);
    CHECK(IsNear(render(-0.2f).At(16, 16), {0, 0, 0, 255}));
}

TEST_CASE("Renderer3D suns glow well beyond their core and burn orange at sunset", "[gpu]") {
    RequireGpu();

    // Looking straight at the sun, high and then just above the horizon. The view is 60 degrees tall, so the 5-degree
    // sun spans a few pixels of radius, and its glow several times that.
    const auto look = [](Spot::Vector3 toSun) {
        Spot::Camera3D camera;
        camera.LookAt(toSun);
        return RenderSky({.sunDirection = -toSun, .sunSize = Spot::ToRadians(5.0f)}, camera);
    };
    const auto high = look({0.0f, 1.0f, -1.0f});
    const auto low = look({0.0f, 0.06f, -1.0f});

    const Pixel noon = high.At(16, 16);
    const int glow = high.At(16, 11)[0];
    const int sky = high.At(16, 1)[0];
    INFO("Noon sun " << int{noon[0]} << ", " << int{noon[1]} << ", " << int{noon[2]} << "; glow " << glow << "; sky "
                     << sky);
    CHECK(std::min({noon[0], noon[1], noon[2]}) > 240);
    // About two sun radii from its center, the glow still outshines the sky.
    CHECK(glow > 200);
    CHECK(sky < glow - 20);

    const Pixel sunset = low.At(16, 16);
    INFO("Sunset sun " << int{sunset[0]} << ", " << int{sunset[1]} << ", " << int{sunset[2]});
    CHECK(sunset[0] > 240);
    CHECK(sunset[2] < sunset[0] - 60);
}

TEST_CASE("Renderer3D skies show the sun's disc where its light comes from", "[gpu]") {
    RequireGpu();

    // Looking straight at a sun high in the sky, then at the same sky away from it.
    Spot::Camera3D camera;
    camera.LookAt({0.0f, 1.0f, -1.0f});
    const Spot::SkyData sky{.sunDirection = {0.0f, -1.0f, 1.0f}, .sunSize = Spot::ToRadians(5.0f)};
    const Pixel sun = RenderSky(sky, camera).At(16, 16);
    camera.LookAt({0.0f, 1.0f, 1.0f});
    const Pixel away = RenderSky(sky, camera).At(16, 16);

    CHECK(std::min({sun[0], sun[1], sun[2]}) > 240);
    CHECK(away[0] < 200);
}

TEST_CASE("Models imported as assets load onto the GPU and draw textured", "[gpu]") {
    RequireGpu();
    const TempDirectory project;
    const auto assets = std::filesystem::path(SPOT_TEST_ASSETS_DIR);
    const auto sources = project.GetPath() / "assets";
    std::filesystem::create_directories(sources / "models");
    std::filesystem::copy_file(assets / "pixels.png", sources / "pixels.png");
    std::filesystem::copy_file(assets / "models" / "textured_quad.gltf", sources / "models" / "textured_quad.gltf");
    std::filesystem::copy_file(assets / "models" / "textured quad.bin", sources / "models" / "textured quad.bin");
    std::filesystem::create_directories(sources / "fonts");
    std::filesystem::copy_file(assets / "fonts" / "FiraSans-Regular.ttf", sources / "fonts" / "FiraSans-Regular.ttf");
    Spot::AssetDatabase(sources, project.GetPath() / "imported").Refresh();
    std::uint32_t mipLevels = 0;
    bool shared = false;
    float fontSize = 0.0f;
    int builtInIndices = 0;

    const TargetRender render(
        32, 32,
        [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            const auto model = Spot::Assets::Load<Spot::Model>("models/textured_quad.gltf");
            REQUIRE(model);
            REQUIRE(model->GetParts().size() == 1);
            const auto& part = model->GetParts()[0];
            REQUIRE((part.mesh && part.material && part.material->baseColorTexture));
            mipLevels = part.material->baseColorTexture->GetMipLevelCount();
            shared = Spot::Assets::Load<Spot::Material>(part.material.GetId()).Get() == part.material.Get();
            if (const auto font = Spot::Assets::Load<Spot::Font>("fonts/FiraSans-Regular.ttf")) {
                fontSize = font->GetPixelSize();
            }
            if (const auto cube = Spot::Assets::Load<Spot::Mesh>(Spot::BuiltInAssets::CubeMesh)) {
                builtInIndices = static_cast<int>(cube->GetIndexCount());
            }

            // The same view as the textured model test, without the file's tint.
            Spot::Material untinted = *part.material;
            untinted.baseColor = Spot::Color::White;
            Spot::Renderer3D renderer(
                device, {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawMesh(*part.mesh, untinted);
            renderer.End();
        },
        {.importDirectory = project.GetPath() / "imported"});

    CHECK(mipLevels == 2);
    CHECK(shared);
    CHECK(fontSize == 32.0f);
    CHECK(builtInIndices == 36);
    CHECK(IsNear(render.At(8, 8), {255, 0, 0, 255}));
    CHECK(IsNear(render.At(23, 8), {0, 255, 0, 255}));
    CHECK(IsNear(render.At(8, 23), {0, 0, 255, 255}));
}

namespace {

// A white floor seen from straight above, 10 units up, lit by the lights with no ambient light, with unit cubes
// hovering 2 units over it. In the 64x64 view, X goes right and Z down, and FloorPixel() gives the pixel column or row
// of a floor coordinate.
TargetRender RenderShadows(const std::vector<Spot::LightData>& lights, const std::vector<Spot::Vector3>& cubes,
                           Spot::Renderer3DStats* stats = nullptr) {
    return TargetRender(
        64, 64, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            REQUIRE(renderer.IsValid());
            const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            const auto floor = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(40.0f));
            REQUIRE((cube && floor));

            Spot::Camera3D camera;
            camera.position = {0.0f, 10.0f, 0.0f};
            camera.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
            renderer.Begin(context, target, camera);
            for (const auto& light : lights) {
                renderer.DrawLight(light);
            }
            renderer.DrawMesh(*floor, {});
            for (const auto& position : cubes) {
                renderer.DrawMesh(*cube, {}, Spot::Matrix4::Translation(position));
            }
            renderer.End();
            if (stats) {
                *stats = renderer.GetStats();
            }
        });
}

int FloorPixel(float coordinate) {
    // The camera's 60 degree view spans tan(30 degrees) * 10 units on each side of the center.
    return static_cast<int>(std::floor((coordinate / 5.7735f + 1.0f) * 32.0f));
}

} // namespace

TEST_CASE("Renderer3D directional lights cast shadows when asked", "[gpu]") {
    RequireGpu();
    // Slanted toward +X, so the cube at x = -3 shadows the floor up to about x = -1.75, past what the camera sees of
    // it.
    Spot::LightData sun{.type = Spot::LightType::Directional, .direction = {0.3f, -1.0f, 0.0f}};
    const std::vector<Spot::Vector3> cubes{{-3.0f, 2.0f, 0.0f}};
    Spot::Renderer3DStats stats;
    const auto unshadowed = RenderShadows({sun}, cubes, &stats);
    CHECK(stats.shadowDrawCalls == 0);
    sun.castsShadows = true;
    const auto shadowed = RenderShadows({sun}, cubes, &stats);
    CHECK(stats.shadowDrawCalls > 0);

    const int shadow = FloorPixel(-2.1f);
    const int lit = FloorPixel(2.0f);
    const int middle = FloorPixel(0.0f);
    CHECK(unshadowed.At(shadow, middle)[0] > 200);
    CHECK(shadowed.At(shadow, middle)[0] < 20);
    CHECK(shadowed.At(lit, middle)[0] > 200);
    // Beside the shadow along Z.
    CHECK(shadowed.At(shadow, FloorPixel(1.5f))[0] > 200);
}

TEST_CASE("Renderer3D spot lights cast shadows when asked", "[gpu]") {
    RequireGpu();
    // From 5 units up, the cube's shadow reaches about a unit from the center, past what the camera sees of it.
    const Spot::LightData spot{.type = Spot::LightType::Spot,
                               .position = {0.0f, 5.0f, 0.0f},
                               .direction = {0.0f, -1.0f, 0.0f},
                               .innerConeAngle = Spot::ToRadians(30.0f),
                               .outerConeAngle = Spot::ToRadians(40.0f),
                               .castsShadows = true};
    Spot::Renderer3DStats stats;
    const auto render = RenderShadows({spot}, {{0.0f, 2.0f, 0.0f}}, &stats);
    CHECK(stats.shadowDrawCalls > 0);

    const int middle = FloorPixel(0.0f);
    CHECK(render.At(FloorPixel(0.85f), middle)[0] < 15);
    CHECK(render.At(middle, FloorPixel(-0.85f))[0] < 15);
    CHECK(render.At(FloorPixel(2.0f), middle)[0] > 60);
}

TEST_CASE("Renderer3D point lights cast shadows in every direction when asked", "[gpu]") {
    RequireGpu();
    // From 4 units up, the middle cube's shadow reaches about 1.33 units from the center. The floor past |x| = 4 is
    // seen from the light's +X and -X faces, and the cube at x = -2.5 shadows it around x = -4.8.
    const Spot::LightData point{
        .type = Spot::LightType::Point, .position = {0.0f, 4.0f, 0.0f}, .range = 10.0f, .castsShadows = true};
    const auto render = RenderShadows({point}, {{0.0f, 2.0f, 0.0f}, {-2.5f, 2.0f, 0.0f}});

    const int middle = FloorPixel(0.0f);
    CHECK(render.At(FloorPixel(1.0f), middle)[0] < 15);
    CHECK(render.At(middle, FloorPixel(1.0f))[0] < 15);
    CHECK(render.At(FloorPixel(-4.8f), middle)[0] < 15);
    CHECK(render.At(FloorPixel(2.5f), middle)[0] > 60);
    CHECK(render.At(FloorPixel(4.8f), middle)[0] > 20);
}

TEST_CASE("Renderer3D lights left without shadow tiles light everything", "[gpu]") {
    RequireGpu();
    // Five point lights take 30 of the 32 tiles; a sixth does not fit, but a spot light still does.
    const Spot::LightData point{
        .type = Spot::LightType::Point, .position = {0.0f, 4.0f, 0.0f}, .range = 10.0f, .castsShadows = true};
    const auto sixPoints = RenderShadows({point, point, point, point, point, point}, {{0.0f, 2.0f, 0.0f}});
    const auto fivePoints = RenderShadows({point, point, point, point, point}, {{0.0f, 2.0f, 0.0f}});
    const Spot::LightData spot{.type = Spot::LightType::Spot,
                               .position = {0.0f, 4.0f, 0.0f},
                               .direction = {0.0f, -1.0f, 0.0f},
                               .range = 10.0f,
                               .innerConeAngle = Spot::ToRadians(40.0f),
                               .outerConeAngle = Spot::ToRadians(50.0f),
                               .castsShadows = true};
    const auto pointsAndSpot = RenderShadows({point, point, point, point, point, spot}, {{0.0f, 2.0f, 0.0f}});

    const int middle = FloorPixel(0.0f);
    const int shadow = FloorPixel(1.0f);
    CHECK(fivePoints.At(shadow, middle)[0] < 15);
    CHECK(sixPoints.At(shadow, middle)[0] > 30);
    CHECK(pointsAndSpot.At(shadow, middle)[0] < 15);
}

TEST_CASE("Renderer3D scenes are shadowed by their sky's sun and by the mesh renderers that cast shadows", "[gpu]") {
    RequireGpu();
    const auto render = [](bool cubeCastsShadows) {
        return TargetRender(
            64, 64,
            [=](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const std::shared_ptr<const Spot::Mesh> cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
                const std::shared_ptr<const Spot::Mesh> floor =
                    Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(40.0f));
                REQUIRE((cube && floor));

                Spot::Scene scene;
                scene.CreateEntity("Floor").AddComponent<Spot::MeshRenderer>(floor);
                auto& box = scene.CreateEntity("Cube");
                box.GetTransform().SetPosition({-3.0f, 2.0f, 0.0f});
                box.AddComponent<Spot::MeshRenderer>(cube).SetCastingShadows(cubeCastsShadows);
                auto& sun = scene.CreateEntity("Sun");
                sun.GetTransform().SetRotation(Spot::Quaternion::LookRotation({0.3f, -1.0f, 0.0f}));
                sun.AddComponent<Spot::DirectionalLight>();
                auto& sky = scene.CreateEntity("Sky").AddComponent<Spot::Sky>();
                sky.SetAmbientIntensity(0.0f);
                sky.SetNightColor(Spot::Color::Black);

                Spot::Camera3D camera;
                camera.position = {0.0f, 10.0f, 0.0f};
                camera.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
                renderer.Begin(context, target, camera);
                renderer.DrawScene(scene);
                renderer.End();
            });
    };

    const int middle = FloorPixel(0.0f);
    const auto shadowed = render(true);
    CHECK(shadowed.At(FloorPixel(-2.1f), middle)[0] < 20);
    CHECK(shadowed.At(FloorPixel(2.0f), middle)[0] > 150);
    CHECK(render(false).At(FloorPixel(-2.1f), middle)[0] > 150);
}

namespace {

// Fills a square of the view, from its bottom-left corner.
class UiSquare final : public Spot::Component {
public:
    UiSquare(float side, Spot::Color color) : m_side(side), m_color(color) {}

protected:
    void OnDrawUi(Spot::Renderer2D& renderer, Spot::Vector2 /*viewSize*/) override {
        renderer.DrawQuad({m_side / 2.0f, m_side / 2.0f}, {m_side, m_side}, m_color);
    }

private:
    float m_side;
    Spot::Color m_color;
};

} // namespace

TEST_CASE("Scenes draw their active components' interfaces in pixels from the view's bottom-left corner", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    Spot::Scene scene;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            result = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }
        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        REQUIRE(target);
        scene.CreateEntity("Square").AddComponent<UiSquare>(2.0f, Spot::Color::Green);
        scene.CreateEntity("Disabled").AddComponent<UiSquare>(4.0f, Spot::Color::Blue).SetEnabled(false);
        // Components draw once they started, as they update.
        scene.Update(0.0f);

        Spot::Renderer2D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
        REQUIRE(renderer.IsValid());
        const Spot::Vector2 size{4.0f, 4.0f};
        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        renderer.Begin(pass, Spot::Scene::ScreenCamera(size), size);
        scene.DrawUi(renderer, size);
        renderer.End();
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(result.size() == 4 * 4 * 4);
    const auto pixelAt = [&](int x, int y) {
        const auto* pixel = &result[static_cast<std::size_t>((y * 4 + x) * 4)];
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    constexpr std::array<std::uint8_t, 4> Black{0, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> Green{0, 255, 0, 255};
    // Rows of the texture go down from the top.
    CHECK(pixelAt(0, 3) == Green);
    CHECK(pixelAt(1, 2) == Green);
    CHECK(pixelAt(2, 2) == Black);
    CHECK(pixelAt(1, 1) == Black);
    CHECK(pixelAt(3, 0) == Black);
}

TEST_CASE("SceneRenderer draws scenes through their first camera, with their interfaces over them", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::SceneRenderer> renderer;
    std::vector<std::uint8_t> withCamera;
    std::vector<std::uint8_t> withoutCamera;
    Spot::Scene world;
    Spot::Scene interface;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        ++frame;
        if (frame == 1) {
            target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
            REQUIRE(target);
            renderer = std::make_unique<Spot::SceneRenderer>(
                device,
                Spot::SceneRendererConfig{.targetFormat = Spot::TextureFormat::Rgba8, .background = Spot::Color::Blue});
            REQUIRE(renderer->IsValid());
            interface.CreateEntity("Square").AddComponent<UiSquare>(2.0f, Spot::Color::Green);
            interface.Update(0.0f);
            world.CreateEntity("Camera").AddComponent<Spot::Camera>().SetBackground(Spot::Color::Red);
        } else if (frame == 2) {
            withCamera = device.DownloadTexture(*target);
            world.Clear();
        } else {
            withoutCamera = device.DownloadTexture(*target);
            return;
        }
        const std::array<Spot::Scene*, 2> scenes{&world, &interface};
        renderer->Render(context, scenes, target.get());
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(withCamera.size() == 4 * 4 * 4);
    REQUIRE(withoutCamera.size() == 4 * 4 * 4);
    const auto pixelAt = [](const std::vector<std::uint8_t>& pixels, int x, int y) {
        const auto* pixel = &pixels[static_cast<std::size_t>((y * 4 + x) * 4)];
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    constexpr std::array<std::uint8_t, 4> Red{255, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> Green{0, 255, 0, 255};
    constexpr std::array<std::uint8_t, 4> Blue{0, 0, 255, 255};
    // Rows of the texture go down from the top, and the interface's square is in the bottom-left corner.
    CHECK(pixelAt(withCamera, 0, 3) == Green);
    CHECK(pixelAt(withCamera, 3, 0) == Red);
    CHECK(pixelAt(withoutCamera, 0, 3) == Green);
    CHECK(pixelAt(withoutCamera, 3, 0) == Blue);
}

TEST_CASE("LineRenderer draws lines hidden by meshes, and debug lines over everything when asked", "[gpu]") {
    RequireGpu();
    Spot::DebugDraw::Clear();
    std::uint32_t lineCount = 0;
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D meshes(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            Spot::LineRenderer lines(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            REQUIRE(lines.IsValid());
            const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            REQUIRE(cube != nullptr);
            const Spot::Camera3D camera = MakeFrontCamera();
            meshes.Begin(context, target, camera);
            meshes.DrawLight(FrontLight);
            meshes.DrawMesh(*cube, {.baseColor = Spot::Color::Red});
            meshes.End();

            // Both lines run behind the cube, across the whole view.
            Spot::DebugDraw::Line({0.0f, -5.0f, -2.0f}, {0.0f, 5.0f, -2.0f},
                                  {.color = Spot::Color::Blue, .depthTest = false});
            Spot::RenderPassDesc over = target;
            over.clearColor = std::nullopt;
            over.clearDepth = std::nullopt;
            auto pass = context.BeginRenderPass(over);
            lines.Begin(pass, camera.GetViewProjection({32.0f, 32.0f}));
            lines.DrawLine({-5.0f, 0.0f, -2.0f}, {5.0f, 0.0f, -2.0f}, Spot::Color::Green);
            lines.DrawDebugLines();
            lines.End();
            lineCount = lines.GetLineCount();
        });
    Spot::DebugDraw::Clear();

    CHECK(lineCount == 2);
    const auto rowHas = [&](int y, int fromX, int toX, Pixel color) {
        for (int x = fromX; x <= toX; ++x) {
            if (IsNear(render.At(x, y), color)) {
                return true;
            }
        }
        return false;
    };
    const auto columnHas = [&](int x, int fromY, int toY, Pixel color) {
        for (int y = fromY; y <= toY; ++y) {
            if (IsNear(render.At(x, y), color)) {
                return true;
            }
        }
        return false;
    };
    constexpr Pixel Green{0, 255, 0, 255};
    constexpr Pixel Blue{0, 0, 255, 255};
    // The green line shows beside the cube and is hidden behind it; the blue one shows over it.
    CHECK(columnHas(1, 12, 19, Green));
    CHECK_FALSE(columnHas(16, 12, 19, Green));
    CHECK(rowHas(1, 12, 19, Blue));
    CHECK(rowHas(16, 12, 19, Blue));
    CHECK(IsNear(render.At(20, 20), {255, 0, 0, 255}));
}

namespace {

float HalfToFloat(std::uint16_t half) {
    const int exponent = (half >> 10) & 0x1f;
    const int mantissa = half & 0x3ff;
    const float value = exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
                                      : std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, exponent - 15);
    return (half & 0x8000) != 0 ? -value : value;
}

// Draws into targets on the first frame, and reads back the one the drawing returns on the second.
std::vector<std::uint8_t>
RenderAndDownload(const std::function<const Spot::Texture*(Spot::GraphicsDevice&, Spot::RenderContext&)>& draw) {
    int frame = 0;
    const Spot::Texture* result = nullptr;
    std::vector<std::uint8_t> pixels;
    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        if (++frame == 1) {
            result = draw(app.GetGraphicsDevice(), context);
        } else if (frame == 2 && result) {
            pixels = app.GetGraphicsDevice().DownloadTexture(*result);
        }
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);
    return pixels;
}

Pixel FirstPixel(const std::vector<std::uint8_t>& pixels) {
    return {pixels[0], pixels[1], pixels[2], pixels[3]};
}

} // namespace

TEST_CASE("Float render targets keep values above 1, in half precision", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::FullscreenPass> solid;
    const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
        CHECK(device.SupportsRenderTarget(Spot::TextureFormat::Rgba16Float));
        target = device.CreateTexture(
            {.width = 4, .height = 4, .format = Spot::TextureFormat::Rgba16Float, .renderTarget = true});
        solid = Spot::FullscreenPass::Create(device, Shaders::SolidFrag,
                                             {.targetFormat = Spot::TextureFormat::Rgba16Float});
        REQUIRE((target && solid));
        auto pass = context.BeginRenderPass({.target = target.get()});
        pass.PushFragmentUniforms(0, Spot::Color{2.5f, 0.001f, 0.5f, 1.0f});
        solid->Draw(pass);
        return target.get();
    });

    REQUIRE(pixels.size() == 4 * 4 * Spot::GetBytesPerPixel(Spot::TextureFormat::Rgba16Float));
    std::array<std::uint16_t, 4> halves{};
    std::memcpy(halves.data(), pixels.data(), sizeof(halves));
    CHECK(HalfToFloat(halves[0]) == Catch::Approx(2.5f));
    CHECK(HalfToFloat(halves[1]) == Catch::Approx(0.001f).margin(0.00001f));
    CHECK(HalfToFloat(halves[2]) == Catch::Approx(0.5f));
    CHECK(HalfToFloat(halves[3]) == Catch::Approx(1.0f));
}

TEST_CASE("Fullscreen passes cover the target, with coordinates from its top-left corner", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::FullscreenPass> uv;
    const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        uv = Spot::FullscreenPass::Create(device, Shaders::UvFrag);
        REQUIRE((target && uv));
        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        uv->Draw(pass);
        return target.get();
    });

    REQUIRE(pixels.size() == 4 * 4 * 4);
    const auto at = [&](int x, int y) {
        const auto* pixel = &pixels[static_cast<std::size_t>((y * 4 + x) * 4)];
        return Pixel{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    // Pixel centers, an eighth of the target in from its edges.
    CHECK(IsNear(at(0, 0), {32, 32, 0, 255}));
    CHECK(IsNear(at(3, 0), {223, 32, 0, 255}));
    CHECK(IsNear(at(0, 3), {32, 223, 0, 255}));
    CHECK(IsNear(at(3, 3), {223, 223, 0, 255}));
}

TEST_CASE("Multisampled float targets resolve into textures that later passes sample", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> samples;
    std::unique_ptr<Spot::Texture> resolved;
    std::unique_ptr<Spot::Texture> output;
    std::unique_ptr<Spot::Sampler> sampler;
    std::unique_ptr<Spot::FullscreenPass> solid;
    std::unique_ptr<Spot::FullscreenPass> textured;
    const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
        constexpr auto Hdr = Spot::TextureFormat::Rgba16Float;
        REQUIRE(device.SupportsSampleCount(Hdr, 4));
        samples =
            device.CreateTexture({.width = 4, .height = 4, .format = Hdr, .renderTarget = true, .sampleCount = 4});
        resolved = device.CreateTexture({.width = 4, .height = 4, .format = Hdr, .renderTarget = true});
        output = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        sampler = device.CreateSampler({.filter = Spot::TextureFilter::Nearest});
        solid = Spot::FullscreenPass::Create(device, Shaders::SolidFrag, {.targetFormat = Hdr, .sampleCount = 4});
        textured = Spot::FullscreenPass::Create(device, Shaders::TexturedFrag);
        REQUIRE((samples && resolved && output && sampler && solid && textured));

        // Brighter than white, so a quarter of it shows it survived the resolve.
        {
            auto pass = context.BeginRenderPass(
                {.target = samples.get(), .clearColor = Spot::Color::Black, .resolveTarget = resolved.get()});
            pass.PushFragmentUniforms(0, Spot::Color{2.0f, 1.0f, 0.5f, 1.0f});
            solid->Draw(pass);
        }
        auto pass = context.BeginRenderPass({.target = output.get(), .clearColor = Spot::Color::Black});
        pass.PushFragmentUniforms(0, Spot::Color{0.25f, 0.25f, 0.25f, 1.0f});
        const std::array inputs{Spot::FullscreenInput{resolved.get(), sampler.get()}};
        textured->Draw(pass, inputs);
        return output.get();
    });

    REQUIRE(pixels.size() == 4 * 4 * 4);
    CHECK(IsNear(FirstPixel(pixels), {128, 64, 32, 255}));
}

TEST_CASE("Packed float targets hold HDR color where the GPU renders into them", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> packed;
    std::unique_ptr<Spot::Texture> output;
    std::unique_ptr<Spot::Sampler> sampler;
    std::unique_ptr<Spot::FullscreenPass> textured;
    bool supported = false;
    const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
        supported = device.SupportsRenderTarget(Spot::TextureFormat::R11G11B10Float);
        if (!supported) {
            return static_cast<const Spot::Texture*>(nullptr);
        }
        packed = device.CreateTexture(
            {.width = 4, .height = 4, .format = Spot::TextureFormat::R11G11B10Float, .renderTarget = true});
        output = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        sampler = device.CreateSampler();
        textured = Spot::FullscreenPass::Create(device, Shaders::TexturedFrag);
        REQUIRE((packed && output && sampler && textured));
        context.BeginRenderPass({.target = packed.get(), .clearColor = Spot::Color{4.0f, 1.0f, 0.5f, 1.0f}}).End();
        auto pass = context.BeginRenderPass({.target = output.get(), .clearColor = Spot::Color::Black});
        pass.PushFragmentUniforms(0, Spot::Color{0.25f, 0.25f, 0.25f, 1.0f});
        const std::array inputs{Spot::FullscreenInput{packed.get(), sampler.get()}};
        textured->Draw(pass, inputs);
        return static_cast<const Spot::Texture*>(output.get());
    });
    if (!supported) {
        WARN("This GPU does not render into R11G11B10Float");
        return;
    }
    REQUIRE(pixels.size() == 4 * 4 * 4);
    // Packed floats have no alpha, which samples as 1.
    CHECK(IsNear(FirstPixel(pixels), {255, 64, 32, 255}));
}
