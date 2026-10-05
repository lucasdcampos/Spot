#include "quad.vert.h"
#include "textured.frag.h"
#include "textured_quad.vert.h"
#include "triangle.frag.h"
#include "triangle.vert.h"

#include "temp_directory.h"

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
    explicit RenderingApplication(std::function<void(Spot::Application&, Spot::RenderContext&)> render,
                                  Spot::AssetConfig assets = {})
        : Application(
              {.window = {.title = "Spot GPU Tests", .width = 320, .height = 240}, .assets = std::move(assets)}),
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

namespace {

using Pixel = std::array<std::uint8_t, 4>;

// Renders one frame into a color and depth target of the given size and reads it back. With several samples, it draws
// into multisampled targets resolved into the color target.
class TargetRender {
public:
    using Draw = std::function<void(Spot::GraphicsDevice&, Spot::RenderPass&, Spot::Vector2 viewSize)>;

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
                auto pass = context.BeginRenderPass({.target = samples ? samples.get() : target.get(),
                                                     .clearColor = Spot::Color::Black,
                                                     .depthTarget = depth.get(),
                                                     .clearDepth = 1.0f,
                                                     .resolveTarget = samples ? target.get() : nullptr});
                draw(device, pass, {static_cast<float>(width), static_cast<float>(height)});
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

    const TargetRender render(32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
        Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
        REQUIRE(renderer.IsValid());
        const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
        REQUIRE(cube != nullptr);

        // The large far cube is drawn last but stays behind the small near one.
        renderer.Begin(pass, MakeFrontCamera(), view);
        renderer.DrawLight(FrontLight);
        renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Green});
        renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Red},
                          Spot::Matrix4::Translation({0.0f, 0.0f, -2.0f}) * Spot::Matrix4::Scale({3.0f, 3.0f, 3.0f}));
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
            16, 16, [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const auto plane = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(2.0f));
                REQUIRE(plane != nullptr);

                // The plane faces +Y; a quarter turn around X makes it face away from the camera.
                renderer.Begin(pass, MakeFrontCamera(), view);
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
            64, 64, [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
                Spot::Renderer3D renderer(
                    device, {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
                const auto plane = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane());
                const std::shared_ptr<Spot::Texture> texture =
                    device.CreateTexture(Spot::Image(2, 1, {255, 255, 255, 255, 0, 0, 0, 255}));
                REQUIRE((plane && texture));

                const Spot::Material material{.baseColorTexture = Spot::AssetRef<Spot::Texture>(texture),
                                              .worldTextureSize = projected ? 1.0f : 0.0f};
                renderer.Begin(pass, MakeFrontCamera(), view);
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
            64, 64, [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
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
                renderer.Begin(pass, camera, view);
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
            [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
                Spot::Renderer3D renderer(device,
                                          {.targetFormat = Spot::TextureFormat::Rgba8, .sampleCount = sampleCount});
                REQUIRE(renderer.IsValid());
                const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
                REQUIRE(cube != nullptr);
                renderer.Begin(pass, MakeFrontCamera(), view);
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
    const TargetRender render(32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
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

        Spot::Renderer3D renderer(device,
                                  {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
        renderer.Begin(pass, MakeFrontCamera(), view);
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

    const TargetRender render(32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
        Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
        const std::shared_ptr<const Spot::Mesh> cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
        REQUIRE(cube != nullptr);

        Spot::Scene scene;
        scene.CreateEntity("Near").AddComponent<Spot::MeshRenderer>(cube,
                                                                    Spot::Material{.baseColor = Spot::Color::Green});

        // The far cube gets its place and size from its parent.
        auto& parent = scene.CreateEntity("Parent");
        parent.GetTransform().SetPosition({0.0f, 0.0f, -2.0f});
        parent.GetTransform().SetScale({3.0f, 3.0f, 3.0f});
        parent.CreateChild("Far").AddComponent<Spot::MeshRenderer>(cube, Spot::Material{.baseColor = Spot::Color::Red});

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

        renderer.Begin(pass, MakeFrontCamera(), view);
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
    return TargetRender(32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
        Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
        const auto cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
        REQUIRE(cube != nullptr);
        renderer.Begin(pass, MakeFrontCamera(), view);
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

TEST_CASE("Renderer3D lights scenes with their active lights", "[gpu]") {
    RequireGpu();

    const TargetRender render(32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
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

        renderer.Begin(pass, MakeFrontCamera(), view);
        renderer.DrawScene(scene);
        renderer.End();
    });

    CHECK(IsNear(render.At(16, 16), {0, 255, 0, 255}));
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
    std::uint32_t mipLevels = 0;
    bool shared = false;
    float fontSize = 0.0f;
    int builtInIndices = 0;

    const TargetRender render(32, 32,
                              [&](Spot::GraphicsDevice& device, Spot::RenderPass& pass, Spot::Vector2 view) {
                                  const auto model = Spot::Assets::Load<Spot::Model>("models/textured_quad.gltf");
                                  REQUIRE(model);
                                  REQUIRE(model->GetParts().size() == 1);
                                  const auto& part = model->GetParts()[0];
                                  REQUIRE((part.mesh && part.material && part.material->baseColorTexture));
                                  mipLevels = part.material->baseColorTexture->GetMipLevelCount();
                                  shared = Spot::Assets::Load<Spot::Material>(part.material.GetId()).Get() ==
                                           part.material.Get();
                                  if (const auto font = Spot::Assets::Load<Spot::Font>("fonts/FiraSans-Regular.ttf")) {
                                      fontSize = font->GetPixelSize();
                                  }
                                  if (const auto cube = Spot::Assets::Load<Spot::Mesh>(Spot::BuiltInAssets::CubeMesh)) {
                                      builtInIndices = static_cast<int>(cube->GetIndexCount());
                                  }

                                  // The same view as the textured model test, without the file's tint.
                                  Spot::Material untinted = *part.material;
                                  untinted.baseColor = Spot::Color::White;
                                  Spot::Renderer3D renderer(device, {.filter = Spot::TextureFilter::Nearest,
                                                                     .targetFormat = Spot::TextureFormat::Rgba8});
                                  renderer.Begin(pass, MakeFrontCamera(), view);
                                  renderer.DrawLight(FrontLight);
                                  renderer.DrawMesh(*part.mesh, untinted);
                                  renderer.End();
                              },
                              {.importDirectory = project.GetPath() / "imported", .sourceDirectory = sources});

    CHECK(mipLevels == 2);
    CHECK(shared);
    CHECK(fontSize == 32.0f);
    CHECK(builtInIndices == 36);
    CHECK(IsNear(render.At(8, 8), {255, 0, 0, 255}));
    CHECK(IsNear(render.At(23, 8), {0, 255, 0, 255}));
    CHECK(IsNear(render.At(8, 23), {0, 0, 255, 255}));
}
