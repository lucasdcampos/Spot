#include "quad.vert.h"
#include "textured.frag.h"
#include "textured_quad.vert.h"
#include "triangle.frag.h"
#include "triangle.vert.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// Runs OnRender() for a few frames, giving up after a bounded number of updates in case no frame can be drawn.
class RenderingApplication final : public Spot::Application {
public:
    explicit RenderingApplication(std::function<void(Spot::Application&, Spot::RenderContext&)> render)
        : Application({.window = {.title = "Spot GPU Tests", .width = 320, .height = 240}}),
          m_render(std::move(render)) {}

    int GetRenderedFrames() const { return m_renderedFrames; }

protected:
    void OnUpdate(float) override {
        if (++m_updates > 200) {
            Quit();
        }
    }

    void OnRender(Spot::RenderContext& context) override {
        m_render(*this, context);
        if (++m_renderedFrames == 3) {
            Quit();
        }
    }

private:
    std::function<void(Spot::Application&, Spot::RenderContext&)> m_render;
    int m_updates = 0;
    int m_renderedFrames = 0;
};

void RequireGpu() {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No GPU backend is available on this machine");
    }
}

} // namespace

TEST_CASE("Application renders frames sized to the window in pixels", "[gpu]") {
    RequireGpu();
    std::string backend;
    int width = 0;
    int pixelWidth = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        backend = app.GetGraphicsDevice().GetBackendName();
        width = context.GetWidth();
        pixelWidth = app.GetWindow().GetPixelWidth();
        auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Blue});
        CHECK(pass.GetNativeHandle() != nullptr);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
    CHECK_FALSE(backend.empty());
    CHECK(width == pixelWidth);
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
