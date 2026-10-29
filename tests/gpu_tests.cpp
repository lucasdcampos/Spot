#include "depth_compare.frag.h"
#include "indirect_arguments.comp.h"
#include "quad.vert.h"
#include "registered_assets.h"
#include "solid.frag.h"
#include "surface.frag.h"
#include "surface.vert.h"
#include "surface_depth.frag.h"
#include "surface_depth.vert.h"
#include "textured.frag.h"
#include "textured_quad.vert.h"
#include "triangle.frag.h"
#include "triangle.vert.h"
#include "uv.frag.h"
#include "volume.frag.h"
#include <spot/assets/primitive_meshes.h>
#include <spot/scene/rendering/render_hook_system.h>
#include <spot/scene/ui/ui_draw_listener.h>
#include <spot/scene/ui/ui_system.h>

#ifdef SPOT_SANDBOX_IMPORTED_DIR
#include "../sandbox/src/menu_thumbnails.h"
#endif

#include "log_capture.h"
#include "rendering_application.h"
#include "temp_directory.h"

#include <spot.h>
#include <spot_import.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Spot::Tests::RenderingApplication;
using Spot::Tests::RequireGpu;

TEST_CASE("Pixel-perfect quads stay sharp and stable across subpixel movement and viewport resizing",
          "[gpu][pixelperfect][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    auto texture = device->CreateTexture({.width = 2, .height = 1});
    REQUIRE(texture);
    device->UploadToTexture(*texture, std::array<std::uint8_t, 8>{255, 0, 0, 255, 0, 255, 0, 255});
    Spot::Renderer2D renderer(*device,
                              {.filter = Spot::TextureFilter::Linear, .targetFormat = Spot::TextureFormat::Rgba8});
    Spot::Camera2D camera;
    camera.pixelPerfect = {.enabled = true, .pixelsPerUnit = 1, .referenceResolution = {32, 24}};
    camera.position = {0.1f, 0.1f};
    auto render = [&](std::uint32_t width, std::uint32_t height, Spot::Vector2 position) {
        auto target = device->CreateTexture(
            {.width = width, .height = height, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
        REQUIRE(target);
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
        const Spot::Vector2 size{static_cast<float>(width), static_cast<float>(height)};
        renderer.Begin(pass, camera, size);
        renderer.DrawQuad(position, {2, 1}, *texture);
        renderer.End();
        pass.End();
        device->EndFrame();
        return device->DownloadTexture(*target);
    };
    for (auto size : {std::pair{64u, 48u}, std::pair{65u, 49u}, std::pair{96u, 72u}}) {
        camera.position = {0.1f, 0.1f};
        const auto initial = render(size.first, size.second, {0.1f, 0.5f});
        camera.position = {0.2f, 0.2f};
        CHECK(render(size.first, size.second, {0.2f, 0.6f}) == initial);
        const int scale = static_cast<int>(
            camera.pixelPerfect.GetZoom({static_cast<float>(size.first), static_cast<float>(size.second)}));
        int red = 0, green = 0, intermediate = 0;
        for (std::size_t i = 0; i < initial.size(); i += 4) {
            if (initial[i] == 255 && initial[i + 1] == 0)
                ++red;
            if (initial[i] == 0 && initial[i + 1] == 255)
                ++green;
            if ((initial[i] > 0 && initial[i] < 255) || (initial[i + 1] > 0 && initial[i + 1] < 255))
                ++intermediate;
        }
        CHECK(red == scale * scale);
        CHECK(green == scale * scale);
        CHECK(intermediate == 0);
    }
    const auto capture = Spot::Process::GetEnvironmentValue("SPOT_2D_CAPTURE_DIR");
    if (capture && !capture->empty()) {
        std::filesystem::create_directories(*capture);
        const auto pixels = render(96, 72, {0.1f, 0.5f});
        CHECK(Spot::Image(96, 72, pixels).SavePng(std::filesystem::path(*capture) / "pixel_perfect.png"));
    }
}

TEST_CASE("Tilemaps render atlas cells holes ordering and transforms offscreen", "[gpu][tilemap][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    auto texture = device->CreateTexture({.width = 2, .height = 1});
    REQUIRE(texture);
    device->UploadToTexture(*texture, std::array<std::uint8_t, 8>{255, 0, 0, 255, 0, 255, 0, 255});
    auto atlas = std::make_shared<Spot::SpriteAtlas>();
    atlas->texture = std::shared_ptr<const Spot::Texture>(std::move(texture));
    atlas->textureSize = {2, 1};
    atlas->sprites = {{"red", {0, 0}, {1, 1}}, {"green", {1, 0}, {1, 1}}};
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    entity.GetTransform().SetPosition({16, 16, 0});
    auto& map = entity.AddComponent<Spot::Tilemap>();
    map.SetAtlas(atlas);
    Spot::TilemapData data;
    data.Resize(2, 2);
    data.cellSize = {16, 16};
    data.definitions = {{0, true}, {1, false}};
    data.SetCell(0, 0, 0);
    data.SetCell(1, 0, 1);
    data.SetCell(1, 1, 0);
    REQUIRE(map.SetData(data));
    auto& overlay = scene.CreateEntity();
    overlay.GetTransform().SetPosition({40, 40, 0});
    auto& sprite = overlay.AddComponent<Spot::SpriteRenderer>();
    sprite.SetSize({8, 8});
    sprite.SetColor(Spot::Color::Blue);
    sprite.SetOrder(1);
    auto target =
        device->CreateTexture({.width = 64, .height = 64, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    REQUIRE(target);
    Spot::Renderer2D renderer(*device,
                              {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
    auto render = [&] {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
        renderer.Begin(pass, Spot::UiSystem::ScreenCamera({64, 64}), {64, 64});
        Spot::DrawScene(renderer, scene);
        renderer.End();
        pass.End();
        device->EndFrame();
        return device->DownloadTexture(*target);
    };
    const auto pixel = [](int x, int y) { return static_cast<std::size_t>((y * 64 + x) * 4); };
    const auto image = render();
    REQUIRE(image.size() == 64 * 64 * 4);
    CHECK(render() == image);
    CHECK(renderer.GetStats().quadCount == 4);
    CHECK(image[pixel(24, 40)] == 255);
    CHECK(image[pixel(40, 40) + 1] == 255);
    CHECK(image[pixel(24, 24)] == 0);
    CHECK(image[pixel(40, 24) + 2] == 255);
    map.SetOrder(2);
    const auto above = render();
    CHECK(above[pixel(40, 24)] == 255);
    CHECK(above[pixel(40, 24) + 2] == 0);
    map.SetEnabled(false);
    const auto disabled = render();
    CHECK(disabled[pixel(24, 40)] == 0);
    map.SetEnabled(true);
    entity.GetTransform().SetPosition({48, 16, 0});
    entity.GetTransform().SetScale({-1, 1, 1});
    const auto mirrored = render();
    CHECK(mirrored[pixel(40, 40)] == 255);
    CHECK(mirrored[pixel(24, 40) + 1] == 255);
    const auto capture = Spot::Process::GetEnvironmentValue("SPOT_2D_CAPTURE_DIR");
    if (capture && !capture->empty()) {
        std::filesystem::create_directories(*capture);
        CHECK(Spot::Image(64, 64, image).SavePng(std::filesystem::path(*capture) / "tilemap.png"));
    }
}

TEST_CASE("Sprite atlas regions and animation render repeated offscreen frames", "[gpu][sprites][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    auto texture = device->CreateTexture({.width = 4, .height = 1});
    REQUIRE(texture);
    device->UploadToTexture(
        *texture, std::array<std::uint8_t, 16>{255, 0, 0, 255, 0, 0, 255, 255, 0, 255, 0, 255, 255, 255, 0, 255});
    auto atlas = std::make_shared<Spot::SpriteAtlas>();
    atlas->texture = std::shared_ptr<const Spot::Texture>(std::move(texture));
    atlas->textureSize = {4, 1};
    atlas->sprites = {{"first", {0, 0}, {2, 1}}, {"second", {2, 0}, {2, 1}}};
    atlas->animations = {{"walk", true, {{0, 0.1f}, {1, 0.2f}}}};
    REQUIRE(atlas->IsValid());
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    entity.GetTransform().SetPosition({32, 16, 0});
    auto& sprite = entity.AddComponent<Spot::SpriteRenderer>();
    sprite.SetSize({64, 32});
    auto& animator = entity.AddComponent<Spot::SpriteAnimator>();
    animator.SetAtlas(atlas);
    REQUIRE(animator.Play("walk"));
    auto target =
        device->CreateTexture({.width = 64, .height = 32, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    REQUIRE(target);
    Spot::Renderer2D renderer(*device,
                              {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
    auto render = [&] {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
        renderer.Begin(pass, Spot::UiSystem::ScreenCamera({64, 32}), {64, 32});
        Spot::DrawScene(renderer, scene);
        renderer.End();
        pass.End();
        device->EndFrame();
        return device->DownloadTexture(*target);
    };
    const auto first = render();
    REQUIRE(first.size() == 64 * 32 * 4);
    CHECK(render() == first);
    const auto pixel = [](int x) { return static_cast<std::size_t>((16 * 64 + x) * 4); };
    CHECK(first[pixel(8)] == 255);
    CHECK(first[pixel(56) + 2] == 255);
    scene.Update(0.11f);
    const auto second = render();
    CHECK(second[pixel(8) + 1] == 255);
    CHECK(second[pixel(56)] == 255);
    CHECK(second[pixel(56) + 1] == 255);
    sprite.SetFlippedX(true);
    const auto flipped = render();
    CHECK(flipped[pixel(8)] == 255);
    CHECK(flipped[pixel(56)] == 0);
    CHECK(flipped[pixel(56) + 1] == 255);
}

TEST_CASE("The developer console renders and reads back offscreen without creating a window",
          "[gpu][console][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    CHECK_FALSE(device->IsVSyncEnabled());
    device->SetVSync(true);
    CHECK_FALSE(device->IsVSyncEnabled());
    auto target = device->CreateTexture(
        {.width = 640, .height = 480, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    REQUIRE(target);
    auto font = Spot::Font::Load(*device,
                                 std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "fonts" / "FiraSans-Regular.ttf", 20.0f);
    REQUIRE(font);
    Spot::Renderer2D renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8});
    REQUIRE(renderer.IsValid());
    Spot::Console console;
    Spot::CVar<int> count(console, "count", 1);
    console.Execute("count 7");
    Spot::InputState state;
    Spot::ConsoleInput input(console, state);
    input.SetOpen(true);
    input.SetLine("count");
    auto render = [&] {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        CHECK(context->GetWidth() == 640);
        CHECK(context->GetHeight() == 480);
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Blue});
        renderer.Begin(pass, Spot::Camera2D{.position = {320.0f, 240.0f}}, {640.0f, 480.0f});
        Spot::DrawConsoleOverlay(console, input, *font, renderer, {640.0f, 480.0f});
        if (!input.IsOpen()) {
            Spot::RuntimeStats stats;
            stats.fps = 60.0;
            stats.frameMilliseconds = 1000.0 / 60.0;
            stats.bodies = 42;
            Spot::DrawStatsOverlay(stats, *font, renderer, {640.0f, 480.0f});
        }
        renderer.End();
        pass.End();
        device->EndFrame();
        return device->DownloadTexture(*target);
    };
    const auto open = render();
    REQUIRE(open.size() == 640 * 480 * 4);
    CHECK(open[2] < 80);
    CHECK(open[(640 * 479) * 4 + 2] == 255);
    bool hasText = false;
    for (std::size_t i = 0; i < 640 * 200 * 4; i += 4) {
        hasText = hasText || open[i] > 100;
    }
    CHECK(hasText);
    input.SetOpen(false);
    const auto closed = render();
    CHECK(closed[(640 - 1) * 4 + 2] < 100);
    CHECK(closed[(640 * 479) * 4 + 2] == 255);
    auto invalid = device->CreateTexture({.width = 8, .height = 8});
    REQUIRE(invalid);
    CHECK_FALSE(device->BeginOffscreenFrame(*invalid));
}

TEST_CASE("Complete frames redirect, present and save RGBA and BGRA screenshots without a window",
          "[gpu][capture][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    TempDirectory directory;
    for (const auto format : {Spot::TextureFormat::Rgba8, Spot::TextureFormat::Bgra8, Spot::TextureFormat::Rgba8Srgb,
                              Spot::TextureFormat::Bgra8Srgb}) {
        CAPTURE(static_cast<int>(format));
        auto frame = device->CreateTexture({.width = 64, .height = 48, .format = format, .renderTarget = true});
        auto screenshot = device->CreateTexture({.width = 64, .height = 48, .format = format, .renderTarget = true});
        auto wrongSize = device->CreateTexture({.width = 8, .height = 8, .format = format, .renderTarget = true});
        REQUIRE(frame);
        REQUIRE(screenshot);
        REQUIRE(wrongSize);
        Spot::Renderer2D renderer(*device, {.targetFormat = format});
        REQUIRE(renderer.IsValid());
        auto* context = device->BeginOffscreenFrame(*frame);
        REQUIRE(context);
        CHECK_FALSE(context->SetFrameTarget(wrongSize.get()));
        REQUIRE(context->SetFrameTarget(screenshot.get()));
        Spot::Profiler::Clear();
        Spot::Profiler::BeginFrame();
        {
            auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Red});
            CHECK_FALSE(context->SetFrameTarget(nullptr));
            CHECK_FALSE(context->BlitTextureToFrame(*frame));
            renderer.Begin(pass, Spot::Camera2D{.position = {32.0f, 24.0f}}, {64.0f, 48.0f});
            renderer.DrawQuad({32.0f, 24.0f}, {20.0f, 14.0f}, Spot::Color::Green);
            renderer.End();
        }
        Spot::Profiler::EndFrame();
        const auto profiles = Spot::Profiler::GetFrames();
        REQUIRE(profiles.size() == 1);
        CHECK(Spot::Profiler::GetCounter(*profiles.back(), "2D draw calls") == 1);
        CHECK(Spot::Profiler::GetCounter(*profiles.back(), "2D triangles") == 2);
        REQUIRE(context->SetFrameTarget(nullptr));
        CHECK_FALSE(context->BlitTextureToFrame(*frame));
        REQUIRE(context->BlitTextureToFrame(*screenshot));
        device->EndFrame();
        CHECK(device->DownloadTexture(*frame) == device->DownloadTexture(*screenshot));
        const auto path = directory.GetPath() / "nested" / "frame.png";
        REQUIRE(Spot::SaveScreenshot(*device, *screenshot, path));
        const auto image = Spot::Image::Load(path);
        REQUIRE(image);
        CHECK(image->GetWidth() == 64);
        CHECK(image->GetHeight() == 48);
        const auto pixels = image->GetPixels();
        REQUIRE(pixels.size() == 64 * 48 * 4);
        CHECK(pixels[0] == 255);
        CHECK(pixels[1] == 0);
        CHECK(pixels[2] == 0);
        const auto center = (24 * 64 + 32) * 4;
        CHECK(pixels[center] == 0);
        CHECK(pixels[center + 1] == 255);
        CHECK(pixels[center + 2] == 0);
    }
    auto unsupported = device->CreateTexture({.width = 8, .height = 8, .format = Spot::TextureFormat::Rgba16Float});
    REQUIRE(unsupported);
    CHECK_FALSE(Spot::SaveScreenshot(*device, *unsupported, directory.GetPath() / "bad.png"));
}

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

TEST_CASE("Cube textures round-trip their faces at each mip level", "[gpu]") {
    RequireGpu();
    // Each face a color of its own.
    std::vector<std::uint8_t> pixels;
    for (std::uint8_t face = 0; face < 6; ++face) {
        for (int texel = 0; texel < 4; ++texel) {
            pixels.insert(pixels.end(),
                          {static_cast<std::uint8_t>(40 * face), 7, static_cast<std::uint8_t>(face), 255});
        }
    }
    std::vector<std::uint8_t> level0;
    std::vector<std::uint8_t> level1;
    std::uint32_t layers = 0;
    bool oblongRejected = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        if (!level0.empty()) {
            return;
        }
        auto& device = app.GetGraphicsDevice();
        const auto cube =
            device.CreateTexture({.width = 2, .height = 2, .type = Spot::TextureType::Cube, .mipmaps = true});
        REQUIRE(cube != nullptr);
        layers = cube->GetLayerCount();
        oblongRejected = device.CreateTexture({.width = 2, .height = 1, .type = Spot::TextureType::Cube}) == nullptr;
        device.UploadToTexture(*cube, pixels);
        level0 = device.DownloadTexture(*cube);
        level1 = device.DownloadTexture(*cube, 1);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(layers == 6);
    CHECK(oblongRejected);
    CHECK(level0 == pixels);
    // The second level is a texel per face, the average of the face's.
    REQUIRE(level1.size() == 6 * 4);
    for (std::size_t face = 0; face < 6; ++face) {
        INFO("Face " << face);
        CHECK(level1[face * 4] == 40 * face);
        CHECK(level1[face * 4 + 2] == face);
    }
}

TEST_CASE("Render passes draw into a face and mip level of a target, and mipmaps are generated from the first level",
          "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> cube;
    std::unique_ptr<Spot::Texture> flat;
    std::vector<std::uint8_t> cubeLevel1;
    std::vector<std::uint8_t> flatLevel2;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            cubeLevel1 = device.DownloadTexture(*cube, 1);
            flatLevel2 = device.DownloadTexture(*flat, 2);
            return;
        }
        if (frame != 1) {
            return;
        }
        cube = device.CreateTexture(
            {.width = 4, .height = 4, .type = Spot::TextureType::Cube, .renderTarget = true, .mipmaps = true});
        flat = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true, .mipmaps = true});
        REQUIRE((cube && flat));
        for (std::uint32_t face = 0; face < 6; ++face) {
            context.BeginRenderPass(
                {.target = cube.get(), .targetLayer = face, .targetMipLevel = 1, .clearColor = Spot::Color::Black});
        }
        context.BeginRenderPass(
            {.target = cube.get(), .targetLayer = 2, .targetMipLevel = 1, .clearColor = Spot::Color::Red});
        context.BeginRenderPass({.target = flat.get(), .clearColor = Spot::Color::Green});
        context.GenerateMipmaps(*flat);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    // Level 1 of the cube is 2x2 per face; only the third face, +Y, is red.
    REQUIRE(cubeLevel1.size() == 6 * 2 * 2 * 4);
    for (std::size_t face = 0; face < 6; ++face) {
        INFO("Face " << face);
        CHECK(cubeLevel1[face * 16] == (face == 2 ? 255 : 0));
        CHECK(cubeLevel1[face * 16 + 12] == (face == 2 ? 255 : 0));
    }
    REQUIRE(flatLevel2.size() == 4);
    CHECK(std::vector<std::uint8_t>(flatLevel2.begin(), flatLevel2.end()) == std::vector<std::uint8_t>{0, 255, 0, 255});
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
        Spot::DrawScene(renderer, scene);
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

// Even matte surfaces reflect about 1% of the light that faces them, which the tolerance allows.
bool IsNear(Pixel a, Pixel b) {
    return std::ranges::all_of(std::array{0, 1, 2, 3}, [&](int i) { return std::abs(a[i] - b[i]) <= 3; });
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
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
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

TEST_CASE("Renderer3D preserves pixel art filtering per material with shared textures", "[gpu][material-filter]") {
    RequireGpu();
    for (const bool world : {false, true}) {
        const TargetRender render(
            96, 48,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(
                    device, {.filter = Spot::TextureFilter::Linear, .targetFormat = Spot::TextureFormat::Rgba8});
                const auto plane = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane());
                const std::shared_ptr<Spot::Texture> texture =
                    device.CreateTexture(Spot::Image(2, 1, {255, 0, 0, 255, 0, 0, 255, 255}));
                REQUIRE((plane && texture));
                Spot::RenderWorld renderWorld;
                // Identical geometry and texture, with different samplers: batching and binding must retain both.
                renderer.Begin(context, target, MakeFrontCamera());
                std::vector<std::shared_ptr<Spot::Material>> materials;
                for (int i = 0; i < 2; ++i) {
                    auto material = std::make_shared<Spot::Material>(
                        Spot::Material{.baseColor = Spot::Color::Black,
                                       .emissive = Spot::Color::White,
                                       .emissiveTexture = Spot::AssetRef<Spot::Texture>(texture),
                                       .nearestFiltering = i == 1});
                    materials.push_back(material);
                    const auto transform =
                        Spot::Matrix4::Translation({i == 0 ? -0.65f : 0.65f, 0, 0}) *
                        Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({1, 0, 0}, Spot::Pi / 2.0f));
                    if (world) {
                        renderWorld.AddMesh({.mesh = plane.get(), .material = material.get(), .transform = transform});
                    } else {
                        renderer.DrawMesh(*plane, *material, transform);
                    }
                }
                if (world) {
                    renderer.DrawWorld(renderWorld);
                }
                renderer.End();
            });
        int mixedLeft = 0;
        int mixedRight = 0;
        for (int x = 0; x < 96; ++x) {
            const auto pixel = render.At(x, 24);
            if (pixel[0] > 8 && pixel[2] > 8) {
                x < 48 ? ++mixedLeft : ++mixedRight;
            }
        }
        CHECK(mixedLeft > 2);
        CHECK(mixedRight == 0);
    }
}

TEST_CASE("Renderer3D hides back faces unless the material is double-sided", "[gpu]") {
    RequireGpu();

    for (const bool doubleSided : {false, true}) {
        const TargetRender render(
            16, 16,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const auto plane = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane(2.0f));
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
                const auto plane = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane());
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

TEST_CASE("Meshes that project their textures from their own space carry them as they move", "[gpu]") {
    RequireGpu();

    // The plane of the test above, moved half a unit right: projected from the world, the stripes stay where they
    // were, while projected from the plane's own space they move with it, half a repeat, so black and white swap.
    for (const Spot::TextureSpace space : {Spot::TextureSpace::World, Spot::TextureSpace::Object}) {
        const TargetRender render(
            64, 64,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(
                    device, {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
                const auto plane = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane());
                const std::shared_ptr<Spot::Texture> texture =
                    device.CreateTexture(Spot::Image(2, 1, {255, 255, 255, 255, 0, 0, 0, 255}));
                REQUIRE((plane && texture));

                const Spot::Material material{.baseColorTexture = Spot::AssetRef<Spot::Texture>(texture),
                                              .worldTextureSize = 1.0f};
                Spot::RenderWorld world;
                world.AddMesh(
                    {.mesh = plane.get(),
                     .material = &material,
                     .transform = Spot::Matrix4::Translation({0.5f, 0.0f, 0.0f}) *
                                  Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({1, 0, 0}, Spot::Pi / 2.0f)) *
                                  Spot::Matrix4::Scale({2.0f, 1.0f, 2.0f}),
                     .textureSpace = space});
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(FrontLight);
                renderer.DrawWorld(world);
                renderer.End();
            });

        const Pixel white{255, 255, 255, 255};
        const Pixel black{0, 0, 0, 255};
        const bool moved = space == Spot::TextureSpace::Object;
        CHECK(IsNear(render.At(27, 32), moved ? white : black));
        CHECK(IsNear(render.At(37, 32), moved ? black : white));
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
                const auto plane = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane(100.0f));
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
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
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

TEST_CASE("Models draw their transformed submeshes with separate materials, including mirrors", "[gpu][model]") {
    RequireGpu();
    bool mirrored = false;
    bool selected = false;
    bool sceneInstance = false;
    SECTION("Model hierarchy") {
    }
    SECTION("Mirrored model hierarchy") {
        mirrored = true;
    }
    SECTION("Only one submesh") {
        selected = true;
    }
    SECTION("Scene hierarchy") {
        sceneInstance = true;
    }
    SECTION("Mirrored scene hierarchy") {
        sceneInstance = true;
        mirrored = true;
    }

    Spot::Renderer3DStats stats;
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::MeshData mesh;
            for (int half = 0; half < 2; ++half) {
                const float left = half == 0 ? -2.0f : 0.0f;
                const float right = left + 2.0f;
                Spot::MeshData quad;
                quad.vertices = {{{left, -2.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {}},
                                 {{right, -2.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {}},
                                 {{right, 2.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {}},
                                 {{left, 2.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {}}};
                quad.indices = {0, 1, 2, 2, 3, 0};
                mesh.AppendSubmesh(quad);
            }
            Spot::ModelData data;
            data.materials = {{.name = "Red", .metallic = 0.0f, .emissive = Spot::Color::Red},
                              {.name = "Green", .metallic = 0.0f, .emissive = Spot::Color::Green}};
            data.meshes = {{.name = "Quad", .mesh = std::move(mesh), .materials = {0, 1}}};
            data.nodes = {{.name = "Pivot", .transform = Spot::Matrix4::Translation({0.0f, 0.0f, -2.0f})},
                          {.name = "Quad", .transform = {}, .mesh = 0, .parent = 0}};
            const auto model = Spot::Model::Create(device, data);
            REQUIRE(model);
            REQUIRE(model->GetNodes().size() == 2);
            REQUIRE(model->GetParts().size() == 2);
            CHECK(model->GetParts()[0].mesh.Get() == model->GetParts()[1].mesh.Get());

            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            renderer.Begin(context, target, MakeFrontCamera());
            if (sceneInstance) {
                Spot::Scene scene;
                scene.Instantiate(*model, "Model").GetTransform().SetScale({mirrored ? -1.0f : 1.0f, 1.0f, 1.0f});
                Spot::DrawScene(renderer, scene);
            } else if (selected) {
                const auto& part = model->GetParts()[1];
                renderer.DrawMesh(*part.mesh, *part.material, part.transform, true, part.submesh);
                // Invalid selections must not fall back to drawing the whole mesh.
                renderer.DrawMesh(*part.mesh, *part.material, part.transform, true, 42);
            } else {
                renderer.DrawModel(*model, Spot::Matrix4::Scale({mirrored ? -1.0f : 1.0f, 1.0f, 1.0f}));
            }
            renderer.End();
            stats = renderer.GetStats();
        });
    CHECK(stats.drawCalls == (selected ? 1u : 2u));
    CHECK(stats.triangleCount == (selected ? 2u : 4u));
    CHECK(IsNear(render.At(8, 16), selected   ? Pixel{0, 0, 0, 255}
                                   : mirrored ? Pixel{0, 255, 0, 255}
                                              : Pixel{255, 0, 0, 255}));
    CHECK(IsNear(render.At(23, 16), mirrored ? Pixel{255, 0, 0, 255} : Pixel{0, 255, 0, 255}));
}

TEST_CASE("Renderer3D draws the active mesh renderers of a scene at their world transforms", "[gpu]") {
    RequireGpu();
    Spot::Renderer3DStats stats;

    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const std::shared_ptr<const Spot::Mesh> cube =
                Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
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
            Spot::DrawScene(renderer, scene);
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
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
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

TEST_CASE("Renderer3D point lights fall off with the square of the distance", "[gpu]") {
    RequireGpu();

    // The wall's surface is at z = 0.5, so these lights are one and two units from its center.
    const auto centerAt = [](float distance, float intensity, float range = 100.0f) {
        return int{RenderLitWall([&](Spot::Renderer3D& renderer) {
                       renderer.DrawLight({.type = Spot::LightType::Point,
                                           .intensity = intensity,
                                           .position = {0.0f, 0.0f, 0.5f + distance},
                                           .range = range});
                   }).At(16, 16)[0]};
    };
    const int near = centerAt(1.0f, 0.5f);
    INFO("Near " << near);
    CHECK(std::abs(near - 128) <= 3);
    // Twice as far, four times the intensity gives the same light, and the same intensity a quarter of it.
    CHECK(std::abs(centerAt(2.0f, 2.0f) - near) <= 3);
    CHECK(std::abs(centerAt(2.0f, 0.5f) - 32) <= 3);
    // The window takes little up to half the range, and all of it at the range.
    CHECK(centerAt(1.0f, 0.5f, 2.0f) > near * 0.85f);
    CHECK(centerAt(2.0f, 2.0f, 2.0f) == 0);
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

TEST_CASE("Renderer3D reflects lights as highlights, untinted off dielectrics and in their color off metals", "[gpu]") {
    RequireGpu();

    // The light comes from behind the camera, so it reflects straight back at the center of the view only.
    const auto drawLight = [](Spot::Renderer3D& renderer) { renderer.DrawLight(FrontLight); };
    const auto plastic =
        RenderLitWall(drawLight, {.baseColor = Spot::Color{0.1f, 0.0f, 0.0f, 1.0f}, .roughness = 0.5f});
    const auto matteMetal = RenderLitWall(drawLight, {.baseColor = Spot::Color::Blue, .metallic = 1.0f});
    const auto matteBlue = RenderLitWall(drawLight, {.baseColor = Spot::Color::Blue});

    // Dielectrics reflect a little of every color; their highlight is gray over their color, and fades aside.
    const Pixel highlight = plastic.At(16, 16);
    INFO("Highlight " << int{highlight[0]} << ", " << int{highlight[1]} << ", " << int{highlight[2]});
    CHECK(highlight[1] > 25);
    CHECK(highlight[1] == highlight[2]);
    CHECK(highlight[0] > highlight[1] + 20);
    CHECK(plastic.At(0, 16)[1] < highlight[1] / 2);

    // Metals scatter nothing, so a matte one only shows what it reflects, in its own color, darker than a matte
    // dielectric scatters.
    const Pixel metal = matteMetal.At(16, 16);
    INFO("Metal " << int{metal[0]} << ", " << int{metal[1]} << ", " << int{metal[2]});
    CHECK(metal[0] == 0);
    CHECK(metal[2] > 20);
    CHECK(metal[2] < matteBlue.At(16, 16)[2] / 2);
}

TEST_CASE("Renderer3D tilts surfaces by their normal maps and darkens ambient light by occlusion", "[gpu]") {
    RequireGpu();

    // A 1x1 texture of linear data, as normal and occlusion maps are imported.
    const auto renderWall = [](std::array<std::uint8_t, 4> normal, std::array<std::uint8_t, 4> occlusion,
                               const Spot::LightData& light) {
        return TargetRender(
            32, 32,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                const auto makeTexture = [&](std::array<std::uint8_t, 4> texel) {
                    return Spot::AssetRef<Spot::Texture>(device.CreateTexture(
                        Spot::Image(1, 1, {texel.begin(), texel.end()}), false, Spot::TextureFormat::Rgba8));
                };
                const Spot::Material material{.normalTexture = makeTexture(normal),
                                              .occlusionTexture = makeTexture(occlusion)};
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                REQUIRE(cube != nullptr);
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(light);
                renderer.DrawMesh(*cube, material, Spot::Matrix4::Scale({20.0f, 20.0f, 1.0f}));
                renderer.End();
            });
    };
    constexpr std::array<std::uint8_t, 4> Flat{128, 128, 255, 255};
    constexpr std::array<std::uint8_t, 4> Unoccluded{255, 255, 255, 255};

    // Facing the light, the flat map leaves the wall fully lit; tilted 60 degrees along the texture's u, which runs
    // along +X on the cube's front, it gets half the light.
    const int flat = renderWall(Flat, Unoccluded, FrontLight).At(16, 16)[0];
    const int tilted = renderWall({238, 128, 192, 255}, Unoccluded, FrontLight).At(16, 16)[0];
    INFO("Flat " << flat << ", tilted " << tilted);
    CHECK(flat > 250);
    CHECK(std::abs(tilted - 128) < 12);
    // A light from +X lights the tilted wall more than the flat one.
    const Spot::LightData side{.type = Spot::LightType::Directional,
                               .direction = Spot::Normalize(Spot::Vector3{-1.0f, 0.0f, -1.0f})};
    CHECK(renderWall({238, 128, 192, 255}, Unoccluded, side).At(16, 16)[0] >
          renderWall(Flat, Unoccluded, side).At(16, 16)[0] + 40);

    // Occlusion takes ambient light only, and less of it off a white wall, which bounces it back: a quarter of the
    // light, as the map says, leaves about 63%.
    const Spot::LightData ambient{.type = Spot::LightType::Ambient};
    CHECK(IsNear(renderWall(Flat, {64, 0, 0, 255}, ambient).At(16, 16), {160, 160, 160, 255}));
    CHECK(renderWall(Flat, {64, 0, 0, 255}, FrontLight).At(16, 16)[0] > 250);
}

TEST_CASE("Renderer3D tilts projected surfaces by their normal maps along the world's axes", "[gpu]") {
    RequireGpu();

    // A wall facing the front camera and a floor seen from above, their textures projected from the world, with a
    // normal map tilted 60 degrees along the texture's u: +X on both, as GetWorldUv() lays textures out.
    const auto render = [](std::array<std::uint8_t, 4> normal, bool floor, const Spot::LightData& light) {
        return TargetRender(
            32, 32,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                const Spot::Material material{
                    .normalTexture = Spot::AssetRef<Spot::Texture>(device.CreateTexture(
                        Spot::Image(1, 1, {normal.begin(), normal.end()}), false, Spot::TextureFormat::Rgba8)),
                    .worldTextureSize = 1.0f};
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                REQUIRE(cube != nullptr);
                Spot::Camera3D camera = MakeFrontCamera();
                if (floor) {
                    camera.position = {0.0f, 3.0f, 0.0f};
                    camera.LookAt({0.0f, 0.0f, -0.01f});
                }
                renderer.Begin(context, target, camera);
                renderer.DrawLight(light);
                renderer.DrawMesh(*cube, material,
                                  floor ? Spot::Matrix4::Scale({20.0f, 1.0f, 20.0f})
                                        : Spot::Matrix4::Scale({20.0f, 20.0f, 1.0f}));
                renderer.End();
            });
    };
    constexpr std::array<std::uint8_t, 4> Flat{128, 128, 255, 255};
    constexpr std::array<std::uint8_t, 4> Tilted{238, 128, 192, 255};
    const Spot::LightData fromFront{.type = Spot::LightType::Directional,
                                    .direction = Spot::Normalize(Spot::Vector3{-1.0f, 0.0f, -1.0f})};
    const Spot::LightData fromAbove{.type = Spot::LightType::Directional,
                                    .direction = Spot::Normalize(Spot::Vector3{-1.0f, -1.0f, 0.0f})};
    // Lights from +X light the tilted surfaces more than the flat ones.
    const int wallFlat = render(Flat, false, fromFront).At(16, 16)[0];
    const int wallTilted = render(Tilted, false, fromFront).At(16, 16)[0];
    const int floorFlat = render(Flat, true, fromAbove).At(16, 16)[0];
    const int floorTilted = render(Tilted, true, fromAbove).At(16, 16)[0];
    INFO("Wall " << wallFlat << " flat, " << wallTilted << " tilted; floor " << floorFlat << " flat, " << floorTilted
                 << " tilted");
    CHECK(wallTilted > wallFlat + 40);
    CHECK(floorTilted > floorFlat + 40);
}

TEST_CASE("Renderer3D drops cutout pixels below their cutoff and blends transparent surfaces in any order", "[gpu]") {
    RequireGpu();

    // Walls facing the front camera at the depths given, each 2 units wide: a third of the view's width aside the
    // center is left of them.
    struct Wall {
        Spot::Material material;
        float z;
    };
    const auto render = [](const std::vector<Wall>& walls) {
        return TargetRender(
            32, 32,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                // Opaque on its right half only: alpha 0 then 255.
                const Spot::AssetRef<Spot::Texture> halves(device.CreateTexture(
                    Spot::Image(2, 1, {255, 255, 255, 0, 255, 255, 255, 255}), false, Spot::TextureFormat::Rgba8));
                Spot::Renderer3D renderer(
                    device, {.filter = Spot::TextureFilter::Nearest, .targetFormat = Spot::TextureFormat::Rgba8});
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                REQUIRE(cube != nullptr);
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(FrontLight);
                for (const Wall& wall : walls) {
                    Spot::Material material = wall.material;
                    if (material.alphaMode == Spot::AlphaMode::Cutout) {
                        material.baseColorTexture = halves;
                    }
                    renderer.DrawMesh(*cube, material,
                                      Spot::Matrix4::Translation({0.0f, 0.0f, wall.z}) *
                                          Spot::Matrix4::Scale({2.0f, 2.0f, 0.1f}));
                }
                renderer.End();
            });
    };

    const auto cutout = render({{{.alphaMode = Spot::AlphaMode::Cutout}, 0.0f}});
    CHECK(IsNear(cutout.At(10, 16), {0, 0, 0, 255}));
    CHECK(IsNear(cutout.At(22, 16), {255, 255, 255, 255}));

    // Half of a red pane over a blue wall, whichever is drawn first.
    const Wall blue{{.baseColor = Spot::Color::Blue}, 0.0f};
    const Wall pane{{.baseColor = {1.0f, 0.0f, 0.0f, 0.5f}, .alphaMode = Spot::AlphaMode::Transparent}, 1.0f};
    for (const auto& walls : {std::vector{blue, pane}, std::vector{pane, blue}}) {
        const Pixel mixed = render(walls).At(16, 16);
        INFO("Mixed " << int{mixed[0]} << ", " << int{mixed[1]} << ", " << int{mixed[2]});
        CHECK(std::abs(mixed[0] - 128) < 10);
        CHECK(std::abs(mixed[2] - 128) < 10);
    }
    // Two panes blend from the farthest, so the nearest shows most, whichever is drawn first.
    const Wall farPane{{.baseColor = {0.0f, 1.0f, 0.0f, 0.5f}, .alphaMode = Spot::AlphaMode::Transparent}, 0.5f};
    for (const auto& walls : {std::vector{pane, farPane}, std::vector{farPane, pane}}) {
        const Pixel panes = render(walls).At(16, 16);
        INFO("Panes " << int{panes[0]} << ", " << int{panes[1]} << ", " << int{panes[2]});
        CHECK(std::abs(panes[0] - 128) < 10);
        CHECK(std::abs(panes[1] - 64) < 10);
    }
}

TEST_CASE("Renderer3D drops the shadows of cutout pixels, and transparent surfaces cast none", "[gpu]") {
    RequireGpu();

    // A slab two units over the floor, lit from 45 degrees, shadows the floor two units aside.
    const auto shadowed = [](const Spot::Material& material) {
        const TargetRender render(
            64, 64,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                const auto floor = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane(40.0f));
                REQUIRE((cube && floor));
                Spot::Camera3D camera;
                camera.position = {0.0f, 10.0f, 0.0f};
                camera.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
                renderer.Begin(context, target, camera);
                renderer.DrawLight({.type = Spot::LightType::Directional,
                                    .direction = Spot::Normalize(Spot::Vector3{1.0f, -1.0f, 0.0f}),
                                    .castsShadows = true});
                renderer.DrawMesh(*floor, {});
                renderer.DrawMesh(*cube, material,
                                  Spot::Matrix4::Translation({0.0f, 2.0f, 0.0f}) *
                                      Spot::Matrix4::Scale({1.0f, 0.2f, 1.0f}));
                renderer.End();
            });
        // The floor's pixel two units along +X, where the shadow falls.
        return render.At(static_cast<int>((2.0f / 5.7735f + 1.0f) * 32.0f), 32)[0] < 40;
    };
    CHECK(shadowed({}));
    CHECK_FALSE(shadowed({.baseColor = {1.0f, 1.0f, 1.0f, 0.2f}, .alphaMode = Spot::AlphaMode::Cutout}));
    CHECK(shadowed({.baseColor = {1.0f, 1.0f, 1.0f, 0.8f}, .alphaMode = Spot::AlphaMode::Cutout}));
    CHECK_FALSE(shadowed({.baseColor = {1.0f, 1.0f, 1.0f, 0.8f}, .alphaMode = Spot::AlphaMode::Transparent}));
}

namespace {

// A mesh in buffers of its own, drawn as a custom surface of one color by the test's shaders: lit like Renderer3D's
// meshes, and into its shadows and the camera's depth. It counts what it is asked to draw.
class TestSurface final : public Spot::CustomSurface {
public:
    int litDraws = 0;
    int shadowDraws = 0;
    int depthDraws = 0;

    TestSurface(Spot::GraphicsDevice& device, const Spot::MeshData& mesh, Spot::Color color,
                const Spot::Matrix4& transform = {}, bool indirect = false)
        : m_indirect(indirect), m_color(color) {
        std::vector<float> vertices;
        for (const Spot::MeshVertex& vertex : mesh.vertices) {
            const Spot::Vector4 position =
                transform * Spot::Vector4{vertex.position.x, vertex.position.y, vertex.position.z, 1.0f};
            const Spot::Vector4 normal =
                transform * Spot::Vector4{vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0f};
            const Spot::Vector3 unit = Spot::Normalize(Spot::Vector3{normal.x, normal.y, normal.z});
            vertices.insert(vertices.end(), {position.x, position.y, position.z, unit.x, unit.y, unit.z});
        }
        m_vertices = device.CreateBuffer(Spot::BufferUsage::Vertex, vertices);
        m_indices = device.CreateBuffer(Spot::BufferUsage::Index, mesh.indices);
        m_indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        m_vertexShader = device.CreateShader(Shaders::SurfaceVert);
        m_fragmentShader = device.CreateShader(Shaders::SurfaceFrag);
        m_depthVertexShader = device.CreateShader(Shaders::SurfaceDepthVert);
        m_depthFragmentShader = device.CreateShader(Shaders::SurfaceDepthFrag);
        REQUIRE((m_vertices && m_indices && m_vertexShader && m_fragmentShader && m_depthVertexShader &&
                 m_depthFragmentShader));
        if (m_indirect) {
            REQUIRE((device.GetCapabilities().compute && device.GetCapabilities().indirectDraws));
            m_counts = device.CreateBuffer(Spot::BufferUsage::Storage, std::array{m_indexCount});
            const auto shader = device.CreateShader(Shaders::IndirectArgumentsComp);
            REQUIRE((m_counts && shader));
            m_compute = device.CreateComputePipeline({.shader = shader.get()});
            REQUIRE(m_compute);
        }
    }

    void Prepare(const Spot::SurfacePrepareContext& context) override {
        if (m_views.size() <= context.viewIndex) {
            m_views.resize(static_cast<std::size_t>(context.viewIndex) + 1);
        }
        auto& view = m_views[context.viewIndex];
        CHECK_FALSE(view.prepared);
        view.kind = context.kind;
        view.viewProjection = context.viewProjection;
        view.planes = context.planes;
        view.prepared = true;
        if (m_indirect) {
            view.arguments = context.device.CreateBuffer({.usage = Spot::BufferUsage::Indirect, .size = 8 * 4});
            REQUIRE(view.arguments);
            // Opening a compute pass here also proves no renderer pass is open yet.
            auto pass = context.context.BeginComputePass({.buffers = {view.arguments.get()}});
            pass.BindPipeline(*m_compute);
            pass.BindStorageBuffer(0, *m_counts);
            pass.Dispatch(1);
        }
    }

    void DrawLit(const Spot::SurfaceDrawContext& context) override {
        ++litDraws;
        if (!m_litPipeline) {
            m_litPipeline = context.device.CreatePipeline({.vertexShader = m_vertexShader.get(),
                                                           .fragmentShader = m_fragmentShader.get(),
                                                           .vertexBuffers = {GetLayout()},
                                                           .cullMode = Spot::CullMode::Back,
                                                           .targetFormat = context.colorFormat,
                                                           .depth = context.depth,
                                                           .sampleCount = context.sampleCount});
            REQUIRE(m_litPipeline);
        }
        context.pass.BindPipeline(*m_litPipeline);
        context.pass.PushFragmentUniforms(1, m_color);
        Draw(context);
    }

    void DrawShadow(const Spot::SurfaceDrawContext& context) override {
        ++shadowDraws;
        DrawDepthOnly(context, m_shadowPipeline);
    }

    void DrawDepth(const Spot::SurfaceDrawContext& context) override {
        ++depthDraws;
        DrawDepthOnly(context, m_depthPipeline);
    }

private:
    static Spot::VertexBufferLayout GetLayout() {
        return {.stride = 6 * sizeof(float),
                .attributes = {{0, Spot::VertexFormat::Float3, 0}, {1, Spot::VertexFormat::Float3, 3 * sizeof(float)}}};
    }

    void DrawDepthOnly(const Spot::SurfaceDrawContext& context, std::unique_ptr<Spot::Pipeline>& pipeline) {
        if (!pipeline) {
            pipeline = context.device.CreatePipeline({.vertexShader = m_depthVertexShader.get(),
                                                      .fragmentShader = m_depthFragmentShader.get(),
                                                      .vertexBuffers = {GetLayout()},
                                                      .cullMode = Spot::CullMode::Back,
                                                      .depthOnly = true,
                                                      .depth = context.depth});
            REQUIRE(pipeline);
        }
        context.pass.BindPipeline(*pipeline);
        context.pass.PushVertexUniforms(0, context.viewProjection);
        Draw(context);
    }

    void Draw(const Spot::SurfaceDrawContext& context) {
        REQUIRE(context.viewIndex < m_views.size());
        auto& view = m_views[context.viewIndex];
        REQUIRE(view.prepared);
        CHECK(view.kind == context.kind);
        CHECK(view.viewProjection == context.viewProjection);
        CHECK(view.planes == context.planes);
        view.prepared = false;
        Spot::RenderPass& pass = context.pass;
        pass.BindVertexBuffer(0, *m_vertices);
        pass.BindIndexBuffer(*m_indices, Spot::IndexType::UInt32);
        if (m_indirect) {
            pass.DrawIndexedIndirect(*view.arguments);
        } else {
            pass.DrawIndexed(m_indexCount);
        }
    }

    struct View {
        Spot::SurfacePass kind = Spot::SurfacePass::Lit;
        Spot::Matrix4 viewProjection;
        std::array<Spot::Vector4, 6> planes;
        bool prepared = false;
        std::unique_ptr<Spot::Buffer> arguments;
    };
    std::vector<View> m_views;
    bool m_indirect = false;
    std::unique_ptr<Spot::Buffer> m_counts;
    std::unique_ptr<Spot::ComputePipeline> m_compute;
    Spot::Color m_color;
    std::unique_ptr<Spot::Buffer> m_vertices;
    std::unique_ptr<Spot::Buffer> m_indices;
    std::uint32_t m_indexCount = 0;
    std::unique_ptr<Spot::Shader> m_vertexShader;
    std::unique_ptr<Spot::Shader> m_fragmentShader;
    std::unique_ptr<Spot::Shader> m_depthVertexShader;
    std::unique_ptr<Spot::Shader> m_depthFragmentShader;
    std::unique_ptr<Spot::Pipeline> m_litPipeline;
    std::unique_ptr<Spot::Pipeline> m_shadowPipeline;
    std::unique_ptr<Spot::Pipeline> m_depthPipeline;
};

// Lit from an angle, by a light and the ambient light, so the surface's shade depends on its normal.
void DrawAngledLights(Spot::Renderer3D& renderer) {
    renderer.DrawLight({.type = Spot::LightType::Directional,
                        .color = {0.8f, 0.7f, 0.6f},
                        .direction = Spot::Normalize(Spot::Vector3{0.4f, -0.5f, -1.0f})});
    renderer.DrawLight({.type = Spot::LightType::Ambient, .color = {0.15f, 0.2f, 0.25f}});
}

} // namespace

TEST_CASE("Renderer3D lights custom surfaces as it lights its meshes", "[gpu]") {
    RequireGpu();
    const Spot::Color color{0.8f, 0.5f, 0.3f};
    const Spot::Matrix4 turned = Spot::Matrix4::Rotation(
        Spot::Quaternion::FromAxisAngle(Spot::Normalize(Spot::Vector3{1.0f, 1.0f, 0.0f}), 0.6f));
    const auto render = [&](bool asSurface) {
        return TargetRender(
            32, 32,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const Spot::MeshData cube = Spot::PrimitiveMeshes::CreateCube();
                const auto mesh = Spot::Mesh::Create(device, cube);
                TestSurface surface(device, cube, color, turned);
                REQUIRE(mesh);
                renderer.Begin(context, target, MakeFrontCamera());
                DrawAngledLights(renderer);
                if (asSurface) {
                    renderer.DrawSurface(surface);
                } else {
                    renderer.DrawMesh(*mesh, {.baseColor = color}, turned);
                }
                renderer.End();
                CHECK(surface.litDraws == (asSurface ? 1 : 0));
            });
    };
    const TargetRender mesh = render(false);
    const TargetRender surface = render(true);
    // Faces of the turned cube, each lit differently, and the background.
    for (const auto& [x, y] : {std::pair{12, 16}, std::pair{20, 12}, std::pair{16, 22}, std::pair{1, 1}}) {
        CHECK(IsNear(surface.At(x, y), mesh.At(x, y)));
    }
    CHECK(surface.At(12, 16) != surface.At(1, 1));
}

TEST_CASE("Renderer3D draws custom surfaces into its shadows, and shadows them", "[gpu]") {
    RequireGpu();

    // A slab two units over the floor, lit from 45 degrees, shadows the floor two units aside; either may be the
    // custom surface.
    const auto shadowed = [](bool slabIsSurface) {
        int shadowDraws = 0;
        const TargetRender render(
            64, 64,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const Spot::MeshData slabData = Spot::PrimitiveMeshes::CreateCube();
                const Spot::MeshData floorData = Spot::PrimitiveMeshes::CreatePlane(40.0f);
                const Spot::Matrix4 slabAt =
                    Spot::Matrix4::Translation({0.0f, 2.0f, 0.0f}) * Spot::Matrix4::Scale({1.0f, 0.2f, 1.0f});
                const auto slab = Spot::Mesh::Create(device, slabData);
                const auto floor = Spot::Mesh::Create(device, floorData);
                TestSurface surface(device, slabIsSurface ? slabData : floorData, Spot::Color::White,
                                    slabIsSurface ? slabAt : Spot::Matrix4{});
                REQUIRE((slab && floor));
                Spot::Camera3D camera;
                camera.position = {0.0f, 10.0f, 0.0f};
                camera.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
                renderer.Begin(context, target, camera);
                renderer.DrawLight({.type = Spot::LightType::Directional,
                                    .direction = Spot::Normalize(Spot::Vector3{1.0f, -1.0f, 0.0f}),
                                    .castsShadows = true});
                renderer.DrawSurface(surface);
                if (slabIsSurface) {
                    renderer.DrawMesh(*floor, {});
                } else {
                    renderer.DrawMesh(*slab, {}, slabAt);
                }
                renderer.End();
                shadowDraws = surface.shadowDraws;
            });
        CHECK(shadowDraws >= 1);
        // The floor's pixel two units along +X, where the shadow falls, and one on the other side, in the light.
        return render.At(static_cast<int>((2.0f / 5.7735f + 1.0f) * 32.0f), 32)[0] < 40 &&
               render.At(static_cast<int>((-2.0f / 5.7735f + 1.0f) * 32.0f), 32)[0] > 120;
    };
    CHECK(shadowed(true));
    CHECK(shadowed(false));
}

TEST_CASE("Renderer3D draws custom surfaces into the camera's depth", "[gpu]") {
    RequireGpu();
    int depthDraws = 0;
    const TargetRender render(
        16, 16, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            TestSurface surface(device, Spot::PrimitiveMeshes::CreateCube(), Spot::Color::White);
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawSurface(surface);
            renderer.End();
            CHECK(renderer.RenderCameraDepth(context) != nullptr);
            depthDraws = surface.depthDraws;
        });
    CHECK(depthDraws == 1);
}

TEST_CASE("Camera depth batches repeated meshes while preserving their alpha masks", "[gpu]") {
    RequireGpu();
    const bool gpuCulling = GENERATE(false, true);
    const bool cutout = GENERATE(false, true);
    const bool differentCutoff = GENERATE(false, true);
    CAPTURE(gpuCulling, cutout, differentCutoff);
    double draws = 0.0;
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8,
                                               .shadowMapSize = 0,
                                               .localShadowMapSize = 0,
                                               .gpuCulling = gpuCulling});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube);
            Spot::Material red{.baseColor = {1, 0, 0, 0.5f}};
            red.alphaMode = cutout ? Spot::AlphaMode::Cutout : Spot::AlphaMode::Opaque;
            red.alphaCutoff = 0.25f;
            Spot::Material green = red;
            green.baseColor = {0, 1, 0, 0.5f};
            green.alphaCutoff = differentCutoff ? 0.75f : red.alphaCutoff;
            Spot::RenderWorld world;
            Spot::Profiler::BeginFrame();
            renderer.Begin(context, target, MakeFrontCamera());
            for (int i = 0; i < 4; ++i) {
                const Spot::Matrix4 transform =
                    Spot::Matrix4::Translation({-0.6f + static_cast<float>(i) * 0.4f, 0, 0}) *
                    Spot::Matrix4::Scale({0.3f, 0.3f, 0.3f});
                const Spot::Material& material = i % 2 == 0 ? red : green;
                if (gpuCulling) {
                    world.AddMesh({.mesh = cube.get(), .material = &material, .transform = transform});
                } else {
                    renderer.DrawMesh(*cube, material, transform);
                }
            }
            if (gpuCulling) {
                renderer.DrawWorld(world);
            }
            renderer.End();
            REQUIRE(renderer.RenderCameraDepth(context));
            Spot::Profiler::EndFrame();
            draws = Spot::Profiler::GetCounter(*Spot::Profiler::GetFrames().back(), "Camera depth draw calls");
        });
    // GPU worlds already have one batch per material; direct meshes can also share depth across opaque colors.
    CHECK(draws == (gpuCulling || (cutout && differentCutoff) ? 2.0 : 1.0));
}

TEST_CASE("The depth prepass leaves the image as it is without it", "[gpu][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    auto skinnedData = Spot::PrimitiveMeshes::CreateCube(0.5f);
    skinnedData.skin.resize(skinnedData.vertices.size());
    for (auto& skin : skinnedData.skin) {
        skin.joints = {0, 1, 0, 0};
        skin.weights = {0.25f, 0.75f, 0, 0};
    }
    const auto cube = Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreateCube());
    const auto plane = Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreatePlane());
    const auto floor = Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreatePlane(8.0f));
    const auto skinned = Spot::Mesh::Create(*device, skinnedData);
    // Alpha in a checkerboard four squares wide, so cutout materials drop every other square.
    std::vector<std::uint8_t> checker;
    for (int i = 0; i < 16; ++i) {
        const auto alpha = static_cast<std::uint8_t>((i % 4 + i / 4) % 2 == 0 ? 255 : 0);
        checker.insert(checker.end(), {255, 255, 255, alpha});
    }
    const std::shared_ptr<Spot::Texture> holes = device->CreateTexture(Spot::Image(4, 4, std::move(checker)));
    REQUIRE((cube && plane && floor && skinned && holes));

    const Spot::Material gray{.baseColor = {0.6f, 0.6f, 0.6f, 1}};
    const Spot::Material red{.baseColor = {0.9f, 0.2f, 0.1f, 1}, .roughness = 0.4f};
    const Spot::Material leaves{.baseColor = {0.2f, 0.8f, 0.3f, 1},
                                .baseColorTexture = Spot::AssetRef<Spot::Texture>(holes),
                                .alphaMode = Spot::AlphaMode::Cutout,
                                .doubleSided = true,
                                .nearestFiltering = true};
    Spot::Material projected = leaves;
    projected.baseColor = {0.9f, 0.8f, 0.2f, 1};
    projected.worldTextureSize = 0.5f;
    projected.nearestFiltering = false;
    const Spot::Material glass{.baseColor = {0.3f, 0.5f, 1.0f, 0.4f}, .alphaMode = Spot::AlphaMode::Transparent};
    // Cards stand facing the camera, which planes do once turned up.
    const Spot::Matrix4 upright = Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({1, 0, 0}, Spot::Pi / 2.0f));
    const std::array palette{Spot::Matrix4{}, Spot::Matrix4::Translation({0.6f, 0.3f, 0})};
    Spot::Camera3D camera{.position = {0.5f, 2.0f, 5.0f}};
    camera.LookAt({0, 0, 0});

    for (const std::uint32_t samples : {1u, 4u}) {
        for (const bool gpuCulling : {false, true}) {
            CAPTURE(samples, gpuCulling);
            const auto target = device->CreateTexture(
                {.width = 96, .height = 96, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
            const auto color = samples == 1 ? nullptr
                                            : device->CreateTexture({.width = 96,
                                                                     .height = 96,
                                                                     .format = Spot::TextureFormat::Rgba8,
                                                                     .renderTarget = true,
                                                                     .sampleCount = samples});
            const auto depth = device->CreateTexture({.width = 96,
                                                      .height = 96,
                                                      .format = Spot::TextureFormat::Depth32Float,
                                                      .renderTarget = true,
                                                      .sampleCount = samples});
            REQUIRE((target && depth && (color || samples == 1)));
            const auto render = [&](bool depthPrepass, double& prepassDraws) {
                Spot::Renderer3D renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8,
                                                    .sampleCount = samples,
                                                    .shadowMapSize = 256,
                                                    .shadowCascadeCount = 1,
                                                    .localShadowMapSize = 128,
                                                    .gpuCulling = gpuCulling,
                                                    .depthPrepass = depthPrepass});
                REQUIRE(renderer.IsValid());
                Spot::RenderWorld world;
                for (int i = 0; i < 3; ++i) {
                    world.AddMesh(
                        {.mesh = cube.get(),
                         .material = &gray,
                         .transform = Spot::Matrix4::Translation({-1.5f + static_cast<float>(i) * 1.5f, 0, -2}) *
                                      Spot::Matrix4::Scale({0.6f, 0.6f, 0.6f})});
                }
                world.AddMesh({.mesh = plane.get(),
                               .material = &leaves,
                               .transform = Spot::Matrix4::Translation({0.3f, 0.2f, -1.2f}) * upright});
                auto* context = device->BeginOffscreenFrame(*target);
                REQUIRE(context);
                Spot::Profiler::BeginFrame();
                renderer.Begin(*context,
                               {.target = color ? color.get() : target.get(),
                                .clearColor = Spot::Color::Black,
                                .depthTarget = depth.get(),
                                .clearDepth = 1.0f,
                                .resolveTarget = color ? target.get() : nullptr},
                               camera);
                renderer.DrawSky({});
                renderer.DrawLight(
                    {.type = Spot::LightType::Point, .position = {1, 1, 1.5f}, .range = 5, .castsShadows = true});
                renderer.DrawMesh(*floor, gray, Spot::Matrix4::Translation({0, -0.5f, 0}));
                renderer.DrawMesh(*cube, red,
                                  Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({0, 1, 0}, 0.6f)) *
                                      Spot::Matrix4::Scale({0.8f, 0.8f, 0.8f}));
                // Turned inside out, which culls its front faces instead.
                renderer.DrawMesh(*cube, red,
                                  Spot::Matrix4::Translation({-1.2f, 0.2f, -0.6f}) *
                                      Spot::Matrix4::Scale({-0.7f, 0.7f, 0.7f}));
                renderer.DrawMesh(*plane, leaves,
                                  Spot::Matrix4::Translation({-0.4f, 0.3f, 0.9f}) * upright *
                                      Spot::Matrix4::Scale({1.2f, 1.2f, 1.2f}));
                renderer.DrawMesh(*plane, projected,
                                  Spot::Matrix4::Translation({0.7f, 0.1f, 1.3f}) * upright *
                                      Spot::Matrix4::Scale({1.2f, 1.2f, 1.2f}));
                renderer.DrawSkinnedMesh(*skinned, red, palette, Spot::Matrix4::Translation({-0.9f, -0.2f, 1.0f}));
                renderer.DrawMesh(*cube, glass,
                                  Spot::Matrix4::Translation({0.9f, 0.0f, 2.0f}) *
                                      Spot::Matrix4::Scale({0.5f, 0.5f, 0.5f}));
                renderer.DrawWorld(world);
                renderer.End();
                Spot::Profiler::EndFrame();
                prepassDraws =
                    Spot::Profiler::GetCounter(*Spot::Profiler::GetFrames().back(), "Depth prepass draw calls");
                device->EndFrame();
                return device->DownloadTexture(*target);
            };
            double withDraws = 0.0;
            double withoutDraws = 0.0;
            const auto with = render(true, withDraws);
            const auto without = render(false, withoutDraws);
            CHECK(withDraws > 0.0);
            CHECK(withoutDraws == 0.0);
            REQUIRE(with.size() == without.size());
            std::size_t different = 0;
            std::size_t varied = 0;
            for (std::size_t i = 0; i < with.size(); ++i) {
                different += std::abs(static_cast<int>(with[i]) - without[i]) > 3 ? 1 : 0;
                varied += std::abs(static_cast<int>(with[i]) - with[i % 4]) > 10 ? 1 : 0;
            }
            CHECK(different == 0);
            // The sky, the meshes and the holes in the cards, not one color.
            CHECK(varied > 4000);
        }
    }
}

TEST_CASE("Point light shadows keep the tiles whose light and casters stand still", "[gpu][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    const auto cube = Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreateCube(0.5f));
    const auto floor = Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreatePlane(8.0f));
    const auto target =
        device->CreateTexture({.width = 64, .height = 64, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    const auto depth = device->CreateTexture(
        {.width = 64, .height = 64, .format = Spot::TextureFormat::Depth32Float, .renderTarget = true});
    REQUIRE((cube && floor && target && depth));
    Spot::Camera3D camera{.position = {0, 3, 5}};
    camera.LookAt({0, 0, 0});
    // A crate beside the light, which only the face of its cube looking along +X sees.
    const auto render = [&](Spot::Renderer3D& renderer, Spot::Vector3 crate, double& kept) {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        Spot::Profiler::BeginFrame();
        renderer.Begin(
            *context,
            {.target = target.get(), .clearColor = Spot::Color::Black, .depthTarget = depth.get(), .clearDepth = 1.0f},
            camera);
        renderer.DrawLight({.type = Spot::LightType::Ambient, .intensity = 0.1f});
        renderer.DrawLight({.type = Spot::LightType::Point, .position = {0, 1, 0}, .range = 6, .castsShadows = true});
        renderer.DrawMesh(*floor, Spot::Material{}, Spot::Matrix4::Translation({0, -0.5f, 0}));
        renderer.DrawMesh(*cube, Spot::Material{}, Spot::Matrix4::Translation(crate));
        renderer.End();
        Spot::Profiler::EndFrame();
        kept = Spot::Profiler::GetCounter(*Spot::Profiler::GetFrames().back(), "Shadow tiles kept");
        device->EndFrame();
        return device->DownloadTexture(*target);
    };
    const auto same = [](const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::abs(static_cast<int>(a[i]) - b[i]) > 3) {
                return false;
            }
        }
        return true;
    };
    const Spot::Renderer3DConfig config{
        .targetFormat = Spot::TextureFormat::Rgba8, .shadowMapSize = 0, .localShadowMapSize = 128};
    Spot::Renderer3D renderer(*device, config);
    const Spot::Vector3 still{1.5f, 0.3f, 0};
    const Spot::Vector3 moved{1.5f, 0.3f, 0.4f};
    double kept = 0;
    const auto first = render(renderer, still, kept);
    CHECK(kept == 0);
    const auto again = render(renderer, still, kept);
    CHECK(kept == 6);
    CHECK(same(again, first));
    // The faces that see the crate move are drawn again, the others kept, and together they cast what all of them
    // drawn anew would.
    const auto afterMove = render(renderer, moved, kept);
    CHECK(kept >= 1);
    CHECK(kept < 6);
    CHECK_FALSE(same(afterMove, first));
    Spot::Renderer3D fresh(*device, config);
    double freshKept = 0;
    CHECK(same(afterMove, render(fresh, moved, freshKept)));
    CHECK(same(render(renderer, still, kept), first));
}

TEST_CASE("Renderer3D prepares custom surfaces with compute before every view", "[gpu]") {
    RequireGpu();
    const auto render = [](bool indirect) {
        return TargetRender(
            32, 32,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                if (!device.GetCapabilities().compute || !device.GetCapabilities().indirectDraws) {
                    SKIP("This device runs no compute shaders or indirect draws");
                }
                Spot::Renderer3D renderer(
                    device,
                    {.targetFormat = Spot::TextureFormat::Rgba8, .shadowMapSize = 32, .localShadowMapSize = 16});
                TestSurface surface(device, Spot::PrimitiveMeshes::CreateCube(), Spot::Color::White, {}, indirect);
                for (int view = 0; view < 2; ++view) {
                    Spot::Camera3D camera = MakeFrontCamera();
                    camera.position.x += static_cast<float>(view) * 0.25f;
                    renderer.Begin(context, target, camera);
                    renderer.DrawSurface(surface);
                    renderer.DrawLight(
                        {.type = Spot::LightType::Directional, .direction = {0, -1, -1}, .castsShadows = true});
                    renderer.DrawLight(
                        {.type = Spot::LightType::Point, .position = {1, 1, 1}, .range = 8, .castsShadows = true});
                    renderer.DrawLight({.type = Spot::LightType::Spot,
                                        .position = {-1, 1, 1},
                                        .direction = {0, -1, -1},
                                        .range = 8,
                                        .castsShadows = true});
                    renderer.End();
                    REQUIRE(renderer.RenderCameraDepth(context));
                }
                CHECK(surface.litDraws == 2);
                CHECK(surface.depthDraws == 2);
                CHECK(surface.shadowDraws == 2 * (4 + 6 + 1));
            });
    };
    const TargetRender direct = render(false);
    const TargetRender indirect = render(true);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            REQUIRE(IsNear(indirect.At(x, y), direct.At(x, y)));
        }
    }
    CHECK(direct.At(16, 16) != direct.At(0, 0));
}

TEST_CASE("Renderer3D draws the custom surfaces of a scene's surface renderers on layers the camera sees", "[gpu]") {
    RequireGpu();
    const auto draws = [](std::uint32_t cullingMask) {
        int litDraws = 0;
        const TargetRender render(
            8, 8, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                auto surface =
                    std::make_shared<TestSurface>(device, Spot::PrimitiveMeshes::CreateCube(), Spot::Color::White);
                Spot::Scene scene;
                Spot::Entity& entity = scene.CreateEntity("Terrain");
                entity.SetLayer(3);
                entity.AddComponent<Spot::SurfaceRenderer>(surface);
                Spot::Camera3D camera = MakeFrontCamera();
                camera.cullingMask = cullingMask;
                renderer.Begin(context, target, camera);
                Spot::DrawScene(renderer, scene);
                renderer.End();
                litDraws = surface->litDraws;
            });
        return litDraws;
    };
    CHECK(draws(0xFFFFFFFFu) == 1);
    CHECK(draws(~(1u << 3)) == 0);
}

TEST_CASE("Renderer3D leaves meshes out of the view out of its pass, and counts them", "[gpu]") {
    RequireGpu();
    Spot::Renderer3DStats stats;
    double profiled = 0.0;
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube != nullptr);
            Spot::Profiler::BeginFrame();
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Red});
            // Behind the camera, and far to the side.
            renderer.DrawMesh(*cube, {}, Spot::Matrix4::Translation({0.0f, 0.0f, 6.0f}));
            renderer.DrawMesh(*cube, {}, Spot::Matrix4::Translation({40.0f, 0.0f, 0.0f}));
            renderer.End();
            stats = renderer.GetStats();
            Spot::Profiler::EndFrame();
            profiled = Spot::Profiler::GetCounter(*Spot::Profiler::GetFrames().back(), "Culled meshes");
        });
    CHECK(stats.visibleMeshes == 1);
    CHECK(stats.culledMeshes == 2);
    CHECK(stats.drawCalls == 1);
    CHECK(profiled == 2.0);
    CHECK(IsNear(render.At(16, 16), {255, 0, 0, 255}));
}

TEST_CASE("Renderer3D draws render worlds, kept on the GPU in batches when asked, and follows their changes", "[gpu]") {
    RequireGpu();
    const bool gpuCulling = GENERATE(false, true);
    INFO("GPU culling " << gpuCulling);
    Spot::Renderer3DStats first;
    Spot::Renderer3DStats second;
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8, .gpuCulling = gpuCulling});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube != nullptr);
            const Spot::Material red{.baseColor = Spot::Color::Red};
            const Spot::Material green{.baseColor = Spot::Color::Green};
            Spot::RenderWorld world;
            const auto a = world.AddMesh({.mesh = cube.get(), .material = &red});
            // Three of the same look, one draw: one batch, or three instances.
            for (float x : {40.0f, 44.0f, 48.0f}) {
                world.AddMesh(
                    {.mesh = cube.get(), .material = &green, .transform = Spot::Matrix4::Translation({x, 0.0f, 0.0f})});
            }
            const auto b = world.GetMeshIds().back();
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawWorld(world);
            renderer.End();
            first = renderer.GetStats();

            // Only the moved instances go up again, when kept on the GPU; the green one now hides where the red one
            // was.
            world.SetTransform(a, Spot::Matrix4::Translation({-40.0f, 0.0f, 0.0f}));
            world.SetTransform(b, {});
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawWorld(world);
            renderer.End();
            second = renderer.GetStats();
        });
    CHECK(first.drawCalls == 1);
    CHECK(first.visibleMeshes == 1);
    CHECK(first.culledMeshes == 3);
    CHECK(second.drawCalls == 1);
    // A batch the CPU keeps counts whole; objects drawn one by one count one by one.
    CHECK(second.visibleMeshes == (gpuCulling ? 3u : 1u));
    CHECK(second.culledMeshes == (gpuCulling ? 1u : 3u));
    CHECK(IsNear(render.At(16, 16), {0, 255, 0, 255}));
}

TEST_CASE("Renderer3D draws the level of detail of world objects their screen size picks", "[gpu]") {
    RequireGpu();
    const bool gpuCulling = GENERATE(false, true);
    INFO("GPU culling " << gpuCulling);
    // The level of detail is a cube moved out of the view's middle, so the middle shows which level is drawn.
    const auto drawn = [gpuCulling](float lodScreenSize, float cullScreenSize) {
        const TargetRender render(
            32, 32,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device,
                                          {.targetFormat = Spot::TextureFormat::Rgba8, .gpuCulling = gpuCulling});
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                Spot::MeshData aside = Spot::PrimitiveMeshes::CreateCube();
                for (Spot::MeshVertex& vertex : aside.vertices) {
                    vertex.position.x += 20.0f;
                }
                const auto far = Spot::Mesh::Create(device, aside);
                REQUIRE((cube && far));
                const Spot::Material red{.baseColor = Spot::Color::Red};
                Spot::RenderWorld world;
                world.AddMesh({.mesh = cube.get(),
                               .material = &red,
                               .lods = {{{lodScreenSize > 0.0f ? far.get() : nullptr, lodScreenSize}}},
                               .cullScreenSize = cullScreenSize});
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(FrontLight);
                renderer.DrawWorld(world);
                renderer.End();
            });
        return IsNear(render.At(16, 16), {255, 0, 0, 255});
    };
    CHECK(drawn(0.01f, 0.0f));
    CHECK_FALSE(drawn(100.0f, 0.0f));
    // Without levels of detail, small enough objects are not drawn at all.
    CHECK(drawn(0.0f, 0.01f));
    CHECK_FALSE(drawn(0.0f, 100.0f));
}

TEST_CASE("Renderer3D culls world objects hidden in the last frame of the same view with GPU culling", "[gpu]") {
    RequireGpu();
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8, .gpuCulling = true});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube != nullptr);
            const Spot::Material red{.baseColor = Spot::Color::Red};
            const Spot::Material green{.baseColor = Spot::Color::Green};
            Spot::RenderWorld world;
            // A green wall over the left half of the view hides the cubes behind it; the red one on the right shows.
            world.AddMesh({.mesh = cube.get(),
                           .material = &green,
                           .transform = Spot::Matrix4::Translation({-1.5f, 0.0f, 1.0f}) *
                                        Spot::Matrix4::Scale({3.0f, 6.0f, 0.2f})});
            world.AddMesh(
                {.mesh = cube.get(), .material = &red, .transform = Spot::Matrix4::Translation({1.0f, 0.0f, 0.0f})});
            world.AddMesh(
                {.mesh = cube.get(), .material = &red, .transform = Spot::Matrix4::Translation({-1.5f, 0.0f, -2.0f})});
            for (std::size_t frame = 0; frame < 2; ++frame) {
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(FrontLight);
                renderer.DrawWorld(world);
                renderer.End();
            }
        });
    // The image of the second pass, culled against the first, is what the first showed.
    CHECK(render.At(8, 16)[1] > 100);
    CHECK(render.At(8, 16)[0] < 40);
    CHECK(render.At(24, 16)[0] > 100);
}

TEST_CASE("Renderer3D draws meshes that look the same as instances of one draw, in every pass", "[gpu]") {
    RequireGpu();
    Spot::Renderer3DStats stats;
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            const auto sphere = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateSphere(0.5f));
            REQUIRE((cube && sphere));
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(
                {.type = Spot::LightType::Directional, .direction = {0.0f, 0.0f, -1.0f}, .castsShadows = true});
            // A red row of small cubes across the top half, in an order the renderer has to gather, with green and
            // blue cubes and a sphere between them, and the materials each a copy.
            for (int i = 0; i < 8; ++i) {
                const float x = -1.4f + 0.4f * static_cast<float>(i);
                renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Red},
                                  Spot::Matrix4::Translation({x, 0.5f, 0.0f}) *
                                      Spot::Matrix4::Scale({0.3f, 0.3f, 0.3f}));
                renderer.DrawMesh(*cube, {.baseColor = i % 2 == 0 ? Spot::Color::Green : Spot::Color::Blue},
                                  Spot::Matrix4::Translation({x, -0.5f, 0.0f}) *
                                      Spot::Matrix4::Scale({0.3f, 0.3f, 0.3f}));
            }
            renderer.DrawMesh(*sphere, {.baseColor = Spot::Color::Red},
                              Spot::Matrix4::Translation({0.0f, 0.0f, -3.0f}));
            renderer.End();
            stats = renderer.GetStats();
        });
    // Red cubes, green cubes, blue cubes and the sphere.
    CHECK(stats.visibleMeshes == 17);
    CHECK(stats.drawCalls == 4);
    CHECK(stats.shadowDrawCalls <= 4 * 4);
    CHECK(stats.triangleCount == 16 * 12 + Spot::PrimitiveMeshes::CreateSphere(0.5f).indices.size() / 3);
    // Each instance is where its own transform puts it: the cubes of the top row are red at both ends.
    CHECK(render.At(3, 11)[0] > 200);
    CHECK(render.At(29, 11)[0] > 200);
    CHECK(render.At(3, 11)[1] < 30);
}

TEST_CASE("Renderer3D sorts what it draws by pipeline and material, changing each as rarely as it can", "[gpu]") {
    RequireGpu();
    Spot::Renderer3DStats stats;
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube != nullptr);
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            // Single- and double-sided materials of three colors, drawn mixed up, in a row across the view.
            const std::array<Spot::Material, 6> materials{{
                {.baseColor = Spot::Color::Red},
                {.baseColor = Spot::Color::Green, .doubleSided = true},
                {.baseColor = Spot::Color::Red},
                {.baseColor = Spot::Color::Blue},
                {.baseColor = Spot::Color::Green, .doubleSided = true},
                {.baseColor = Spot::Color::Red, .doubleSided = true},
            }};
            for (std::size_t i = 0; i < materials.size(); ++i) {
                renderer.DrawMesh(*cube, materials[i],
                                  Spot::Matrix4::Translation({-1.25f + 0.5f * static_cast<float>(i), 0.0f, 0.0f}) *
                                      Spot::Matrix4::Scale({0.3f, 0.3f, 0.3f}));
            }
            renderer.End();
            stats = renderer.GetStats();
        });
    // Two pipelines, each with two materials; the two single-sided red cubes are one draw, as are the green ones.
    CHECK(stats.pipelineChanges == 2);
    CHECK(stats.materialChanges == 4);
    CHECK(stats.drawCalls == 4);
    CHECK(render.At(4, 16)[0] > 200);
}

TEST_CASE("Renderer3D draws only the meshes on the layers the camera sees", "[gpu]") {
    RequireGpu();
    Spot::Renderer3DStats stats;
    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const Spot::AssetRef<Spot::Mesh> cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube);
            Spot::Scene scene;
            // A red cube on layer 3 in front of a green one on the default layer.
            auto& hidden = scene.CreateEntity("Hidden");
            hidden.SetLayer(3);
            hidden.GetTransform().SetPosition({0.0f, 0.0f, 1.0f});
            hidden.AddComponent<Spot::MeshRenderer>(cube, Spot::Material{.baseColor = Spot::Color::Red});
            scene.CreateEntity("Shown").AddComponent<Spot::MeshRenderer>(
                cube, Spot::Material{.baseColor = Spot::Color::Green});
            Spot::Camera3D camera = MakeFrontCamera();
            camera.cullingMask &= ~(1u << 3);
            renderer.Begin(context, target, camera);
            renderer.DrawLight(FrontLight);
            Spot::DrawScene(renderer, scene);
            renderer.End();
            stats = renderer.GetStats();
        });
    CHECK(stats.visibleMeshes == 1);
    CHECK(stats.culledMeshes == 0);
    CHECK(IsNear(render.At(16, 16), {0, 255, 0, 255}));

    // The camera component carries its mask into the camera it gives renderers.
    Spot::Scene scene;
    auto& camera = scene.CreateEntity("Camera").AddComponent<Spot::Camera>();
    camera.SetLayerSeen(3, false);
    CHECK_FALSE(camera.SeesLayer(3));
    CHECK(camera.SeesLayer(2));
    CHECK(camera.GetCamera3D().cullingMask == ~(1u << 3));
}

TEST_CASE("Renderer3D lights each part of the view with the many lights near it", "[gpu]") {
    RequireGpu();
    // An 8 by 8 grid of small point lights over the wall, each lighting only the patch in front of it, alternately red
    // and green.
    Spot::Renderer3DStats stats;
    const auto render = RenderLitWall([&](Spot::Renderer3D& renderer) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                renderer.DrawLight(
                    {.type = Spot::LightType::Point,
                     .color = (x + y) % 2 == 0 ? Spot::Color::Red : Spot::Color::Green,
                     .intensity = 0.04f,
                     .position = {-1.4f + 0.4f * static_cast<float>(x), 1.4f - 0.4f * static_cast<float>(y), 0.7f},
                     .range = 0.35f});
            }
        }
    });
    // Each light lights the pixel in front of it in its own color, more than the 16 lights it once could use.
    int lit = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            // The wall is half a unit from the origin, the camera 3 units away, seeing 1.44 units each side at the
            // wall.
            const auto pixel = [](float position) { return static_cast<int>((position / 1.443f + 1.0f) * 16.0f); };
            const Pixel at =
                render.At(pixel(-1.4f + 0.4f * static_cast<float>(x)), pixel(-1.4f + 0.4f * static_cast<float>(y)));
            const bool red = (x + y) % 2 == 0;
            lit += (red ? at[0] > 100 && at[1] < 30 : at[1] > 100 && at[0] < 30) ? 1 : 0;
        }
    }
    CHECK(lit == 64);
}

TEST_CASE("Renderer3D softens shadow edges, and shadows what lies between the sun and its cascades", "[gpu]") {
    RequireGpu();
    // A floor seen from 6 units above, 3.46 units each side, with a coarse shadow map, and a slab over it.
    const auto render = [](Spot::Vector3 slab, Spot::Vector3 size, Spot::Vector3 sun) {
        return TargetRender(
            64, 64,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8,
                                                   .shadowMapSize = 256,
                                                   .shadowCascadeCount = 1,
                                                   .shadowDistance = 20.0f});
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                const auto floor = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane(40.0f));
                REQUIRE((cube && floor));
                Spot::Camera3D camera;
                camera.position = {0.0f, 6.0f, 0.0f};
                camera.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
                renderer.Begin(context, target, camera);
                renderer.DrawLight({.type = Spot::LightType::Directional, .direction = sun, .castsShadows = true});
                renderer.DrawMesh(*floor, {});
                renderer.DrawMesh(*cube, {}, Spot::Matrix4::Translation(slab) * Spot::Matrix4::Scale(size));
                renderer.End();
            });
    };

    // A strip two units up, lit at 45 degrees, shadows the floor from x = -3 to -1, and the light fades over a few
    // pixels across the edge of its shadow.
    const auto near =
        render({-4.0f, 2.0f, 0.0f}, {2.0f, 0.2f, 20.0f}, Spot::Normalize(Spot::Vector3{1.0f, -1.0f, 0.0f}));
    int fading = 0;
    for (int x = 16; x < 30; ++x) {
        const int value = near.At(x, 32)[0];
        fading += value > 20 && value < 150 ? 1 : 0;
    }
    INFO("Fading pixels " << fading);
    CHECK(fading >= 3);
    CHECK(near.At(13, 32)[0] < 20);
    CHECK(near.At(50, 32)[0] > 150);

    // A slab far above the camera, past the cascade's slice, still shadows the floor under it.
    const auto far = render({0.0f, 150.0f, 0.0f}, {8.0f, 0.2f, 8.0f}, {0.0f, -1.0f, 0.0f});
    CHECK(far.At(32, 32)[0] < 20);
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
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
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
            const std::shared_ptr<const Spot::Mesh> cube =
                Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
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
            Spot::DrawScene(renderer, scene);
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
    CHECK(sky[2] > 80);
    CHECK(ground[0] > ground[2] + 40);
}

namespace {

// A sky without a sun, whose light is its night color above the horizon and the ground's color, lit by that, below.
constexpr Spot::SkyData PlainSky{
    .hasSun = false, .groundColor = {1.0f, 0.0f, 0.0f}, .nightColor = {0.2f, 0.4f, 1.0f}, .sunCastsShadows = false};

// A white wall lit only by the sky, seen from the camera.
TargetRender RenderSkyLitWall(const Spot::Camera3D& camera, const Spot::Matrix4& wall,
                              const Spot::Material& material = {}) {
    return TargetRender(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube != nullptr);
            renderer.Begin(context, target, camera);
            renderer.DrawSky(PlainSky);
            renderer.DrawMesh(*cube, material, wall);
            renderer.End();
        });
}

} // namespace

TEST_CASE("Renderer3D lights surfaces with the sky's light from all around", "[gpu]") {
    RequireGpu();

    // A floor seen from above gets the sky's light; a ceiling seen from below, the ground's.
    Spot::Camera3D above;
    above.position = {0.0f, 3.0f, 0.0f};
    above.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    Spot::Camera3D below;
    below.position = {0.0f, -3.0f, 0.0f};
    below.rotation = Spot::Quaternion::LookRotation({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    const auto slab = Spot::Matrix4::Scale({20.0f, 1.0f, 20.0f});
    const Pixel floor = RenderSkyLitWall(above, slab).At(16, 16);
    const Pixel ceiling = RenderSkyLitWall(below, slab).At(16, 16);
    INFO("Floor " << int{floor[0]} << ", " << int{floor[1]} << ", " << int{floor[2]} << "; ceiling " << int{ceiling[0]}
                  << ", " << int{ceiling[1]} << ", " << int{ceiling[2]});
    CHECK(std::abs(floor[0] - 51) < 12);
    CHECK(std::abs(floor[1] - 102) < 12);
    CHECK(floor[2] > 235);
    // The ground is red, lit by the sky: 0.2 of red.
    CHECK(std::abs(ceiling[0] - 51) < 12);
    CHECK(ceiling[2] < 25);

    // A mirror floor reflects the sky above, and a rough metal one blurs in some of the ground.
    const Pixel mirror = RenderSkyLitWall(above, slab, {.metallic = 1.0f, .roughness = 0.0f}).At(16, 16);
    const Pixel rough = RenderSkyLitWall(above, slab, {.metallic = 1.0f, .roughness = 1.0f}).At(16, 16);
    INFO("Mirror " << int{mirror[0]} << ", " << int{mirror[1]} << ", " << int{mirror[2]});
    CHECK(std::abs(mirror[0] - 51) < 8);
    CHECK(std::abs(mirror[1] - 102) < 8);
    CHECK(mirror[2] > 240);
    INFO("Rough " << int{rough[0]} << ", " << int{rough[1]} << ", " << int{rough[2]});
    CHECK(rough[2] < mirror[2]);
    CHECK(rough[1] < mirror[1]);
}

TEST_CASE("Renderer3D captures the sky's light again only when the sky changes", "[gpu]") {
    RequireGpu();
    std::vector<std::uint32_t> updates;
    const TargetRender render(
        8, 8, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            Spot::SkyData sky = PlainSky;
            for (int frame = 0; frame < 4; ++frame) {
                if (frame == 2) {
                    sky.nightColor = Spot::Color::White;
                }
                renderer.Begin(context, target, MakeFrontCamera());
                // A sky that only is drawn has no light to capture.
                renderer.DrawSky(frame == 3 ? Spot::SkyData{.lightsScene = false} : sky);
                renderer.End();
                updates.push_back(renderer.GetStats().skyLightingUpdates);
            }
        });
    CHECK(updates == std::vector<std::uint32_t>{1, 0, 1, 0});
}

TEST_CASE("Renderer3D keeps the sky's light from what is under cover", "[gpu]") {
    RequireGpu();

    // A floor seen from under a roof, which the camera is below; without sky occlusion the roof changes nothing.
    Spot::Camera3D camera;
    camera.position = {0.0f, 2.0f, 0.0f};
    camera.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    const auto renderFloor = [&](bool roof, std::uint32_t skyOcclusionSize) {
        return TargetRender(
                   32, 32,
                   [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                       Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8,
                                                          .skyOcclusionSize = skyOcclusionSize,
                                                          .skyOcclusionDistance = 32.0f});
                       const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                       REQUIRE(cube != nullptr);
                       renderer.Begin(context, target, camera);
                       renderer.DrawSky(PlainSky);
                       renderer.DrawMesh(*cube, {}, Spot::Matrix4::Scale({20.0f, 1.0f, 20.0f}));
                       if (roof) {
                           renderer.DrawMesh(*cube, {},
                                             Spot::Matrix4::Translation({0.0f, 4.0f, 0.0f}) *
                                                 Spot::Matrix4::Scale({20.0f, 1.0f, 20.0f}));
                       }
                       renderer.End();
                   })
            .At(16, 16);
    };
    const Pixel open = renderFloor(false, 128);
    const Pixel covered = renderFloor(true, 128);
    const Pixel ignored = renderFloor(true, 0);
    INFO("Open " << int{open[2]} << ", covered " << int{covered[2]} << ", without occlusion " << int{ignored[2]});
    CHECK(open[2] > 235);
    CHECK(covered[2] < open[2] / 4);
    // What covers it is not black: lit by the red ground, the roof sends some of its light back.
    CHECK(covered[0] > open[0] / 2);
    CHECK(IsNear(ignored, open));
}

TEST_CASE("Renderer3D darkens creases with ambient occlusion from the camera's depth", "[gpu]") {
    RequireGpu();

    // A dark wall standing on a dark floor, lit by ambient light only, seen from above the floor with the crease where
    // they meet in the middle. Lighter surfaces bounce more light into the crease and darken less.
    Spot::Camera3D camera;
    camera.position = {0.0f, 3.0f, 3.0f};
    camera.LookAt({0.0f, 0.5f, 0.0f});
    const auto renderCrease = [&](bool occlusion) {
        return TargetRender(
            64, 64,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8,
                                                   .ambientOcclusion = {.enabled = occlusion, .radius = 1.0f}});
                REQUIRE(renderer.IsValid());
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                REQUIRE(cube != nullptr);
                renderer.Begin(context, target, camera);
                renderer.DrawLight({.type = Spot::LightType::Ambient, .intensity = 2.5f});
                const Spot::Material dark{.baseColor = {0.2f, 0.2f, 0.2f, 1.0f}};
                renderer.DrawMesh(*cube, dark, Spot::Matrix4::Scale({20.0f, 1.0f, 20.0f}));
                renderer.DrawMesh(*cube, dark,
                                  Spot::Matrix4::Translation({0.0f, 1.5f, -0.5f}) *
                                      Spot::Matrix4::Scale({20.0f, 2.0f, 1.0f}));
                renderer.End();
                // The depth it worked from is the camera's, given without drawing it again.
                CHECK(renderer.RenderCameraDepth(context) != nullptr);
            });
    };
    const TargetRender plain = renderCrease(false);
    const TargetRender occluded = renderCrease(true);
    const int nearCrease = plain.At(32, 36)[0] - occluded.At(32, 36)[0];
    const int farFromIt = plain.At(32, 62)[0] - occluded.At(32, 62)[0];
    INFO("Darker by " << nearCrease << " near the crease, " << farFromIt << " far from it");
    CHECK(nearCrease > 20);
    CHECK(std::abs(farFromIt) <= 6);
}

namespace {

// A white floor inside a room whose walls glow red, with no light of its own: only light from all around can reach
// it, which the sky does not give.
TargetRender RenderGlowingRoom(bool probe) {
    return TargetRender(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube != nullptr);
            Spot::Camera3D camera;
            camera.position = {0.0f, 2.0f, 0.0f};
            camera.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
            renderer.Begin(context, target, camera);
            renderer.DrawMesh(*cube,
                              {.baseColor = Spot::Color::Black, .emissive = Spot::Color::Red, .doubleSided = true},
                              Spot::Matrix4::Scale({10.0f, 10.0f, 10.0f}));
            renderer.DrawMesh(
                *cube, {}, Spot::Matrix4::Translation({0.0f, -1.0f, 0.0f}) * Spot::Matrix4::Scale({4.0f, 0.2f, 4.0f}));
            if (probe) {
                renderer.DrawReflectionProbe({.id = 1});
            }
            renderer.End();
            CHECK(renderer.GetStats().reflectionProbeCaptures == (probe ? 1u : 0u));
            CHECK(renderer.GetStats().reflectionProbes == (probe ? 1u : 0u));
        });
}

} // namespace

TEST_CASE("Renderer3D lights what is inside a reflection probe's box with its capture of the scene", "[gpu]") {
    RequireGpu();

    const Pixel without = RenderGlowingRoom(false).At(16, 16);
    const Pixel with = RenderGlowingRoom(true).At(16, 16);
    INFO("Without " << int{without[0]} << ", " << int{without[1]} << ", " << int{without[2]} << "; with "
                    << int{with[0]} << ", " << int{with[1]} << ", " << int{with[2]});
    CHECK(without[0] < 10);
    CHECK(with[0] > 100);
    CHECK(with[1] < 30);
    CHECK(with[2] < 30);
}

TEST_CASE("Renderer3D captures a reflection probe again only when it moves or changes version", "[gpu]") {
    RequireGpu();
    std::vector<std::uint32_t> captures;
    const TargetRender render(
        8, 8, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            Spot::ReflectionProbeData probe{.id = 7};
            for (int frame = 0; frame < 6; ++frame) {
                if (frame == 2) {
                    probe.version = 1;
                }
                if (frame == 4) {
                    probe.position = {1.0f, 0.0f, 0.0f};
                }
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawReflectionProbe(probe);
                renderer.End();
                captures.push_back(renderer.GetStats().reflectionProbeCaptures);
            }
        });
    CHECK(captures == std::vector<std::uint32_t>{1, 0, 1, 0, 1, 0});
}

TEST_CASE("Renderer3D draws the sky behind meshes", "[gpu]") {
    RequireGpu();

    const TargetRender render(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE(cube != nullptr);
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Green});
            // Drawn last, yet behind. It does not light the cube, which shows the front light alone.
            renderer.DrawSky({.lightsScene = false});
            renderer.End();
        });

    CHECK(IsNear(render.At(16, 16), {0, 255, 0, 255}));
    CHECK(render.At(16, 2)[2] > 80);
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

TEST_CASE("Renderer3D skies light the scene with a sun that sets on its own, and a moon that rises", "[gpu]") {
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

    // Once the sun has set behind the wall, the moon rises behind the camera, opposite it, and lights the wall's front
    // bluish, far more faintly than the sun; a hidden moon lights nothing.
    const auto renderNight = [](float moonSize) {
        return RenderLitWall([=](Spot::Renderer3D& renderer) {
            Spot::SkyData sky{
                .sunDirection = {0.0f, 1.0f, 1.0f}, .nightColor = Spot::Color::Black, .ambientIntensity = 0.0f};
            sky.night.moonSize = moonSize;
            renderer.DrawSky(sky);
        });
    };
    const Pixel moonlit = renderNight(Spot::SkyNight{}.moonSize).At(16, 16);
    INFO("Moonlit " << int{moonlit[0]} << ", " << int{moonlit[1]} << ", " << int{moonlit[2]});
    CHECK(moonlit[2] > 5);
    CHECK(moonlit[2] > moonlit[0]);
    CHECK(moonlit[2] < high / 4);
    CHECK(IsNear(renderNight(0.0f).At(16, 16), {0, 0, 0, 255}));
}

TEST_CASE("Renderer3D lights scenes with the sun of their sky through its air", "[gpu]") {
    RequireGpu();

    // A sun below the horizon would light the wall's front on its own, but the sky hides it.
    const auto render = [](float sunHeight) {
        return TargetRender(
            32, 32,
            [=](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const std::shared_ptr<const Spot::Mesh> cube =
                    Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
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
                Spot::DrawScene(renderer, scene);
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
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            const auto floor = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane(40.0f));
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
                               .intensity = 25.0f,
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
    const Spot::LightData point{.type = Spot::LightType::Point,
                                .intensity = 16.0f,
                                .position = {0.0f, 4.0f, 0.0f},
                                .range = 10.0f,
                                .castsShadows = true};
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
    const Spot::LightData point{.type = Spot::LightType::Point,
                                .intensity = 16.0f,
                                .position = {0.0f, 4.0f, 0.0f},
                                .range = 10.0f,
                                .castsShadows = true};
    const auto sixPoints = RenderShadows({point, point, point, point, point, point}, {{0.0f, 2.0f, 0.0f}});
    const auto fivePoints = RenderShadows({point, point, point, point, point}, {{0.0f, 2.0f, 0.0f}});
    const Spot::LightData spot{.type = Spot::LightType::Spot,
                               .intensity = 16.0f,
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
                const std::shared_ptr<const Spot::Mesh> cube =
                    Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                const std::shared_ptr<const Spot::Mesh> floor =
                    Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreatePlane(40.0f));
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
                Spot::DrawScene(renderer, scene);
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
class UiSquare final : public Spot::Component, public Spot::UiDrawListener {
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

class UiDrawRecorder final : public Spot::Component, public Spot::UiDrawListener {
public:
    UiDrawRecorder(std::vector<int>& calls, int value) : m_calls(calls), m_value(value) {}
    void OnDrawUi(Spot::Renderer2D&, Spot::Vector2) override { m_calls.push_back(m_value); }

private:
    std::vector<int>& m_calls;
    int m_value;
};

} // namespace

TEST_CASE("Scenes draw their active components' interfaces in pixels from the view's bottom-left corner", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> target;
    std::vector<std::uint8_t> result;
    Spot::Scene scene;
    int frame = 0;
    std::vector<int> drawCalls;

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
        auto& firstEntity = scene.CreateEntity("Earlier entity");
        scene.CreateEntity("Later entity").AddComponent<UiDrawRecorder>(drawCalls, 1);
        firstEntity.AddComponent<UiDrawRecorder>(drawCalls, 2);

        Spot::Renderer2D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
        REQUIRE(renderer.IsValid());
        const Spot::Vector2 size{4.0f, 4.0f};
        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        renderer.Begin(pass, Spot::UiSystem::ScreenCamera(size), size);
        scene.GetOrAddSystem<Spot::UiSystem>().DrawUi(renderer, size);
        CHECK(drawCalls.empty());
        // Custom overlays wait for OnStart; component addition order determines drawing order.
        scene.Update(0.0f);
        scene.GetOrAddSystem<Spot::UiSystem>().DrawUi(renderer, size);
        CHECK(drawCalls == std::vector<int>{1, 2});
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
    Spot::Scene& interface = world;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        ++frame;
        if (frame == 1) {
            target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
            REQUIRE(target);
            // Without a curve or noise, so colors come out exactly.
            renderer = std::make_unique<Spot::SceneRenderer>(
                device,
                Spot::SceneRendererConfig{.targetFormat = Spot::TextureFormat::Rgba8,
                                          .background = Spot::Color::Blue,
                                          .tonemapping = {.op = Spot::TonemapOperator::Linear, .dither = false}});
            REQUIRE(renderer->IsValid());
            interface.CreateEntity("Square").AddComponent<UiSquare>(2.0f, Spot::Color::Green);
            interface.Update(0.0f);
            world.CreateEntity("Camera").AddComponent<Spot::Camera>().SetBackground(Spot::Color::Red);
        } else if (frame == 2) {
            withCamera = device.DownloadTexture(*target);
            world.FindEntity("Camera")->Destroy();
        } else {
            withoutCamera = device.DownloadTexture(*target);
            return;
        }
        renderer->Render(context, world, target.get());
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
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
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
            lines.DrawLines(Spot::DebugDraw::GetLines());
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

TEST_CASE("HDR sun cores stay brighter than their halo under strong warm sunlight", "[gpu][sky][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    constexpr std::uint32_t Size = 127;
    auto target = device->CreateTexture(
        {.width = Size, .height = Size, .format = Spot::TextureFormat::Rgba16Float, .renderTarget = true});
    auto depth = device->CreateTexture(
        {.width = Size, .height = Size, .format = Spot::TextureFormat::Depth32Float, .renderTarget = true});
    REQUIRE((target && depth));
    Spot::Renderer3D renderer(
        *device,
        {.targetFormat = Spot::TextureFormat::Rgba16Float, .hdr = true, .shadowMapSize = 0, .localShadowMapSize = 0});
    REQUIRE(renderer.IsValid());
    Spot::Camera3D camera;
    camera.LookAt({0, 1, -1});
    for (const float intensity : {1.0f, 5.0f}) {
        CAPTURE(intensity);
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Begin(*context, {.clearColor = Spot::Color::Black, .depthTarget = depth.get(), .clearDepth = 1.0f},
                       camera);
        renderer.DrawSky({.sunDirection = {0, -1, 1},
                          .sunColor = {1.0f, 0.85f, 0.62f},
                          .sunIntensity = intensity,
                          .sunSize = Spot::ToRadians(5.0f),
                          .exposure = 0.42f,
                          .lightsScene = false});
        renderer.End();
        device->EndFrame();
        const auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == Size * Size * 8);
        const auto channel = [&](int x, int component) {
            std::uint16_t half = 0;
            const auto index = (static_cast<std::size_t>(Size / 2) * Size + x) * 8 + component * 2;
            std::memcpy(&half, pixels.data() + index, sizeof(half));
            return HalfToFloat(half);
        };
        for (int component = 0; component < 3; ++component) {
            CAPTURE(component);
            const float core = channel(Size / 2, component);
            const float halo = std::max(channel(Size / 2 - 12, component), channel(Size / 2 + 12, component));
            INFO("Core radiance " << core << "; halo " << halo);
            CHECK(core > halo);
        }
    }
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

TEST_CASE("3D textures are sampled slice by slice, and only sampled", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::Texture> volume;
    std::unique_ptr<Spot::Sampler> sampler;
    std::unique_ptr<Spot::FullscreenPass> slices;
    bool rejected = false;
    const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
        target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
        volume = device.CreateTexture({.width = 1, .height = 1, .type = Spot::TextureType::Texture3D, .depth = 4});
        sampler = device.CreateSampler();
        slices = Spot::FullscreenPass::Create(device, Shaders::VolumeFrag);
        REQUIRE((target && volume && sampler && slices));
        CHECK(volume->GetDepth() == 4);
        CHECK(volume->GetLayerCount() == 4);
        rejected = !device.CreateTexture(
                       {.width = 1, .height = 1, .type = Spot::TextureType::Texture3D, .depth = 2, .mipmaps = true}) &&
                   !device.CreateTexture({.width = 1, .height = 1, .depth = 2});
        // Red, green, blue and white slices, from z = 0.
        const std::array<std::uint8_t, 16> texels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        device.UploadToTexture(*volume, texels);
        auto pass = context.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        const std::array inputs{Spot::FullscreenInput{volume.get(), sampler.get()}};
        slices->Draw(pass, inputs);
        return target.get();
    });

    CHECK(rejected);
    REQUIRE(pixels.size() == 4 * 4 * 4);
    const auto at = [&](int y) {
        const auto* pixel = &pixels[static_cast<std::size_t>(y * 4 * 4)];
        return Pixel{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    CHECK(IsNear(at(0), {255, 0, 0, 255}));
    CHECK(IsNear(at(1), {0, 255, 0, 255}));
    CHECK(IsNear(at(2), {0, 0, 255, 255}));
    CHECK(IsNear(at(3), {255, 255, 255, 255}));
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

TEST_CASE("SceneRenderer draws light into an HDR image, which its tonemapper brings within the target", "[gpu]") {
    RequireGpu();
    // A camera whose background is twice as bright as white: a curve keeps it below the target's white, and exposure
    // darkens it before the curve.
    const auto render = [](const Spot::TonemapSettings& tonemapping, bool fxaa = false) {
        std::unique_ptr<Spot::Texture> target;
        std::unique_ptr<Spot::SceneRenderer> renderer;
        Spot::Scene scene;
        scene.CreateEntity("Camera").AddComponent<Spot::Camera>().SetBackground(Spot::Color{2.0f, 2.0f, 2.0f, 1.0f});
        const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
            target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
            renderer = std::make_unique<Spot::SceneRenderer>(
                device, Spot::SceneRendererConfig{
                            .targetFormat = Spot::TextureFormat::Rgba8, .tonemapping = tonemapping, .fxaa = fxaa});
            REQUIRE(renderer->IsValid());
            renderer->Render(context, scene, target.get());
            return target.get();
        });
        REQUIRE(pixels.size() == 4 * 4 * 4);
        return static_cast<int>(pixels[0]);
    };

    const int linear = render({.op = Spot::TonemapOperator::Linear, .dither = false});
    const int aces = render({.op = Spot::TonemapOperator::Aces, .dither = false});
    const int darker = render({.op = Spot::TonemapOperator::Aces, .exposure = 0.25f, .dither = false});
    const int agx = render({.op = Spot::TonemapOperator::AgX, .dither = false});
    CHECK(linear == 255);
    CHECK(aces > 200);
    CHECK(aces < 255);
    CHECK(darker < aces - 40);
    CHECK(agx > 120);
    CHECK(agx < 255);
    CHECK(agx != aces);
    // FXAA after the tonemapper leaves a flat image as it is.
    CHECK(render({.op = Spot::TonemapOperator::Aces, .dither = false}, true) == aces);
}

TEST_CASE("Bloom spreads light brighter than its threshold, and nothing else", "[gpu]") {
    RequireGpu();
    // A dim image, below the threshold, with a spot 64 times brighter than white in its middle.
    constexpr std::uint16_t Bright = 0x5400;
    constexpr std::uint16_t Dim = 0x3800;
    std::vector<std::uint16_t> image(32 * 32 * 4);
    for (std::size_t pixel = 0; pixel < 32 * 32; ++pixel) {
        const std::size_t x = pixel % 32;
        const std::size_t y = pixel / 32;
        const std::uint16_t value = x >= 15 && x <= 16 && y >= 15 && y <= 16 ? Bright : Dim;
        image[pixel * 4] = image[pixel * 4 + 1] = image[pixel * 4 + 2] = value;
        image[pixel * 4 + 3] = 0x3C00;
    }

    const auto glowOf = [&](const Spot::BloomSettings& settings, bool& rendered) {
        std::unique_ptr<Spot::Texture> source;
        std::unique_ptr<Spot::Bloom> bloom;
        const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
            source = device.CreateTexture({.width = 32, .height = 32, .format = Spot::TextureFormat::Rgba16Float});
            bloom = Spot::Bloom::Create(device);
            REQUIRE((source && bloom));
            device.UploadToTexture(*source, image);
            const Spot::Texture* glow = bloom->Render(context, *source, settings);
            rendered = glow != nullptr;
            return glow;
        });
        // At half the image's size, in red: the middle, a few pixels from it, and a corner.
        const auto at = [&](int x, int y) {
            std::uint16_t half = 0;
            std::memcpy(&half, &pixels[static_cast<std::size_t>((y * 16 + x) * 8)], sizeof(half));
            return HalfToFloat(half);
        };
        return rendered ? std::array{at(8, 8), at(12, 8), at(0, 0)} : std::array{0.0f, 0.0f, 0.0f};
    };

    bool rendered = false;
    const auto glow = glowOf({}, rendered);
    REQUIRE(rendered);
    CHECK(glow[0] > 1.0f);
    CHECK(glow[1] > 0.05f);
    CHECK(glow[1] < glow[0]);
    CHECK(glow[2] < glow[1]);

    // A threshold above the spot leaves nothing to glow, and a scatter of 0 keeps the glow tight.
    const auto none = glowOf({.threshold = 1000.0f}, rendered);
    CHECK(none[0] == Catch::Approx(0.0f).margin(1e-3f));
    const auto tight = glowOf({.scatter = 0.0f}, rendered);
    CHECK(tight[1] < glow[1]);

    glowOf({.intensity = 0.0f}, rendered);
    CHECK_FALSE(rendered);
}

TEST_CASE("FXAA smooths the steps of an edge and leaves flat areas as they are", "[gpu]") {
    RequireGpu();
    // A diagonal edge, white above it and black below, in steps of whole pixels.
    std::vector<std::uint8_t> image(16 * 16 * 4);
    for (std::size_t y = 0; y < 16; ++y) {
        for (std::size_t x = 0; x < 16; ++x) {
            const std::uint8_t value = x > y ? 255 : 0;
            std::uint8_t* pixel = &image[(y * 16 + x) * 4];
            pixel[0] = pixel[1] = pixel[2] = value;
            pixel[3] = 255;
        }
    }
    std::unique_ptr<Spot::Texture> source;
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::Fxaa> fxaa;
    const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
        source = device.CreateTexture({.width = 16, .height = 16});
        target = device.CreateTexture({.width = 16, .height = 16, .renderTarget = true});
        fxaa = Spot::Fxaa::Create(device, Spot::TextureFormat::Rgba8);
        REQUIRE((source && target && fxaa));
        device.UploadToTexture(*source, image);
        auto pass = context.BeginRenderPass({.target = target.get()});
        fxaa->Draw(pass, *source);
        return target.get();
    });

    REQUIRE(pixels.size() == 16 * 16 * 4);
    const auto at = [&](int x, int y) { return static_cast<int>(pixels[static_cast<std::size_t>((y * 16 + x) * 4)]); };
    int shades = 0;
    for (int i = 1; i < 15; ++i) {
        shades += at(i, i) > 16 && at(i, i) < 240 ? 1 : 0;
    }
    CHECK(shades > 6);
    CHECK(at(15, 0) == 255);
    CHECK(at(0, 15) == 0);
}

namespace {

std::uint16_t FloatToHalf(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    const int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    if (value == 0.0f || exponent <= 0) {
        return sign;
    }
    return static_cast<std::uint16_t>(sign | (std::min(exponent, 30) << 10) | ((bits >> 13) & 0x3ffu));
}

// An 8x8 image of the color, through the tonemapper with no curve, graded; the pixels of the result.
std::vector<std::uint8_t> Grade(Spot::Color color, const Spot::GradingSettings& grading,
                                const Spot::Image* lutImage = nullptr) {
    std::vector<std::uint16_t> image(8 * 8 * 4);
    for (std::size_t i = 0; i < image.size(); i += 4) {
        image[i] = FloatToHalf(color.r);
        image[i + 1] = FloatToHalf(color.g);
        image[i + 2] = FloatToHalf(color.b);
        image[i + 3] = FloatToHalf(1.0f);
    }
    std::unique_ptr<Spot::Texture> source;
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::Texture> lut;
    std::unique_ptr<Spot::Tonemapper> tonemapper;
    return RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
        source = device.CreateTexture({.width = 8, .height = 8, .format = Spot::TextureFormat::Rgba16Float});
        target = device.CreateTexture({.width = 8, .height = 8, .renderTarget = true});
        tonemapper = Spot::Tonemapper::Create(device, Spot::TextureFormat::Rgba8);
        REQUIRE((source && target && tonemapper));
        device.UploadToTexture(*source, image);
        Spot::GradingSettings settings = grading;
        if (lutImage) {
            lut = device.CreateTexture(*lutImage, false, Spot::TextureFormat::Rgba8);
            settings.lut = lut.get();
        }
        auto pass = context.BeginRenderPass({.target = target.get()});
        tonemapper->Draw(pass, *source, {.op = Spot::TonemapOperator::Linear, .dither = false}, settings);
        return target.get();
    });
}

int Channel(const std::vector<std::uint8_t>& pixels, int x, int y, int channel) {
    return pixels[static_cast<std::size_t>((y * 8 + x) * 4 + channel)];
}

} // namespace

TEST_CASE("Grading changes contrast, saturation and white balance before the curve", "[gpu]") {
    RequireGpu();
    // Middle grey stays where it is under contrast, while what is brighter gets brighter.
    const auto grey = Grade({0.18f, 0.18f, 0.18f, 1.0f}, {.contrast = 2.0f});
    CHECK(std::abs(Channel(grey, 4, 4, 0) - 46) <= 2);
    const auto plain = Grade({0.3f, 0.3f, 0.3f, 1.0f}, {});
    const auto contrasty = Grade({0.3f, 0.3f, 0.3f, 1.0f}, {.contrast = 2.0f});
    CHECK(Channel(contrasty, 4, 4, 0) > Channel(plain, 4, 4, 0) + 20);

    const auto grey2 = Grade({0.5f, 0.1f, 0.1f, 1.0f}, {.saturation = 0.0f});
    CHECK(std::abs(Channel(grey2, 4, 4, 0) - Channel(grey2, 4, 4, 1)) <= 2);
    CHECK(std::abs(Channel(grey2, 4, 4, 1) - Channel(grey2, 4, 4, 2)) <= 2);

    const auto warm = Grade({0.4f, 0.4f, 0.4f, 1.0f}, {.temperature = 1.0f});
    CHECK(Channel(warm, 4, 4, 0) > Channel(warm, 4, 4, 2) + 20);
    const auto magenta = Grade({0.4f, 0.4f, 0.4f, 1.0f}, {.tint = 1.0f});
    CHECK(Channel(magenta, 4, 4, 1) < Channel(magenta, 4, 4, 0));
}

TEST_CASE("Grading darkens the corners with a vignette and maps colors through a LUT", "[gpu]") {
    RequireGpu();
    const auto vignette = Grade({0.5f, 0.5f, 0.5f, 1.0f}, {.vignette = 1.0f, .vignetteStart = 0.0f});
    CHECK(Channel(vignette, 0, 0, 0) < Channel(vignette, 4, 4, 0) - 30);

    // A neutral LUT changes nothing, and one that inverts the displayed colors inverts them.
    const Spot::Image neutral = Spot::MakeNeutralLut(16);
    CHECK(neutral.GetWidth() == 256);
    CHECK(neutral.GetHeight() == 16);
    const auto same = Grade({0.2f, 0.5f, 0.8f, 1.0f}, {}, &neutral);
    const auto plain = Grade({0.2f, 0.5f, 0.8f, 1.0f}, {});
    for (int channel = 0; channel < 3; ++channel) {
        CHECK(std::abs(Channel(same, 4, 4, channel) - Channel(plain, 4, 4, channel)) <= 3);
    }
    std::vector<std::uint8_t> inverted(neutral.GetPixels().begin(), neutral.GetPixels().end());
    for (std::size_t i = 0; i < inverted.size(); i += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            inverted[i + channel] = static_cast<std::uint8_t>(255 - inverted[i + channel]);
        }
    }
    const Spot::Image invertedLut(256, 16, std::move(inverted));
    const auto black = Grade({1.0f, 1.0f, 1.0f, 1.0f}, {}, &invertedLut);
    CHECK(Channel(black, 4, 4, 0) <= 3);
    const auto halfway = Grade({1.0f, 1.0f, 1.0f, 1.0f}, {.lutContribution = 0.5f}, &invertedLut);
    CHECK(Channel(halfway, 4, 4, 0) > 100);
    CHECK(Channel(halfway, 4, 4, 0) < 150);
}

TEST_CASE("Renderer3D fogs what is far away in the fog's color, and the sky toward the horizon", "[gpu]") {
    RequireGpu();
    // A green cube in front of the camera, three units away, in magenta fog.
    const auto renderCube = [](const std::optional<Spot::FogData>& fog) {
        const TargetRender render(
            8, 8, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                REQUIRE((renderer.IsValid() && cube));
                renderer.Begin(context, target, MakeFrontCamera());
                renderer.DrawLight(FrontLight);
                renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Green});
                if (fog) {
                    renderer.DrawFog(*fog);
                }
                renderer.End();
            });
        return render.At(4, 4);
    };
    constexpr Spot::Color Magenta{1.0f, 0.0f, 1.0f, 1.0f};
    const Pixel clear = renderCube(std::nullopt);
    const Pixel fogged = renderCube(Spot::FogData{.color = Magenta, .density = 1.0f, .heightFalloff = 0.0f});
    const Pixel beyond = renderCube(Spot::FogData{.color = Magenta, .density = 1.0f, .start = 10.0f});
    CHECK(clear[0] < 10);
    CHECK(fogged[0] > 200);
    CHECK(fogged[1] < 40);
    CHECK(IsNear(beyond, clear));

    // Fog that thins with height hides the horizon, not the sky high above it.
    const TargetRender sky(
        32, 32, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            REQUIRE(renderer.IsValid());
            Spot::Camera3D camera = MakeFrontCamera();
            camera.LookAt({0.0f, 1.0f, 0.0f});
            renderer.Begin(context, target, camera);
            renderer.DrawSky({});
            renderer.DrawFog({.color = Magenta, .density = 0.05f, .heightFalloff = 0.2f});
            renderer.End();
        });
    const Pixel horizon = sky.At(16, 31);
    const Pixel high = sky.At(16, 0);
    INFO("Horizon " << int{horizon[0]} << ", " << int{horizon[1]} << ", " << int{horizon[2]} << "; high "
                    << int{high[0]} << ", " << int{high[1]} << ", " << int{high[2]});
    CHECK(horizon[0] > 200);
    CHECK(horizon[1] < 60);
    CHECK(high[1] > horizon[1] + 20);
    CHECK(high[0] < horizon[0] - 100);
}

TEST_CASE("Renderer3D fogs scenes with their first active Fog, from its entity's height", "[gpu]") {
    RequireGpu();
    Spot::Scene scene;
    auto& fog = scene.CreateEntity("Fog").AddComponent<Spot::Fog>();
    fog.color = Spot::Color{1.0f, 0.0f, 1.0f, 1.0f};
    fog.density = 1.0f;
    fog.heightFalloff = 0.0f;
    fog.GetEntity().GetTransform().SetPosition({0.0f, 7.0f, 0.0f});
    CHECK(fog.GetFogData().baseHeight == 7.0f);
    const TargetRender render(
        8, 8, [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
            Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
            const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
            REQUIRE((renderer.IsValid() && cube));
            renderer.Begin(context, target, MakeFrontCamera());
            renderer.DrawLight(FrontLight);
            renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Green});
            Spot::DrawScene(renderer, scene);
            renderer.End();
        });
    CHECK(render.At(4, 4)[0] > 200);
}

TEST_CASE("SceneRenderer takes its look from the scenes' first active PostProcessing", "[gpu]") {
    RequireGpu();
    const auto render = [](bool withLook) {
        std::unique_ptr<Spot::Texture> target;
        std::unique_ptr<Spot::SceneRenderer> renderer;
        Spot::Scene scene;
        scene.CreateEntity("Camera").AddComponent<Spot::Camera>().SetBackground(Spot::Color{0.5f, 0.5f, 0.5f, 1.0f});
        if (withLook) {
            auto& look = scene.CreateEntity("Look").AddComponent<Spot::PostProcessing>();
            look.tonemapper = Spot::TonemapOperator::Linear;
            look.exposure = 0.5f;
            look.bloomIntensity = 0.0f;
        }
        const auto pixels = RenderAndDownload([&](Spot::GraphicsDevice& device, Spot::RenderContext& context) {
            target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
            renderer = std::make_unique<Spot::SceneRenderer>(
                device, Spot::SceneRendererConfig{.targetFormat = Spot::TextureFormat::Rgba8,
                                                  .tonemapping = {.op = Spot::TonemapOperator::Linear, .dither = false},
                                                  .bloom = {.intensity = 0.0f}});
            renderer->Render(context, scene, target.get());
            return target.get();
        });
        REQUIRE(pixels.size() == 4 * 4 * 4);
        return static_cast<int>(pixels[0]);
    };
    // Without it the settings show the grey as it is; with it, its exposure halves it.
    CHECK(std::abs(render(false) - 128) <= 2);
    CHECK(std::abs(render(true) - 64) <= 3);
}

TEST_CASE("SceneRenderer runs render hooks at their stages, its own before the scenes'", "[gpu]") {
    RequireGpu();
    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::SceneRenderer> renderer;
    std::unique_ptr<Spot::FullscreenPass> hdrSolid;
    std::unique_ptr<Spot::FullscreenPass> outputSolid;
    std::vector<std::string> calls;
    Spot::Scene scene;
    scene.CreateEntity("Camera").AddComponent<Spot::Camera>().SetBackground(Spot::Color::Black);

    // A fullscreen color, drawn into whatever the stage's targets are.
    const auto fill = [&](const Spot::RenderStageContext& stage, Spot::Color color) {
        Spot::FullscreenPass& solid = stage.colorFormat == Spot::TextureFormat::Rgba16Float ? *hdrSolid : *outputSolid;
        Spot::RenderPassDesc desc = stage.pass;
        desc.depthTarget = nullptr;
        auto pass = stage.context.BeginRenderPass(desc);
        pass.PushFragmentUniforms(0, color);
        solid.Draw(pass);
    };
    for (const auto stage : {Spot::RenderStage::BeforeOpaque, Spot::RenderStage::AfterOpaque,
                             Spot::RenderStage::AfterTransparent, Spot::RenderStage::BeforePostProcessing,
                             Spot::RenderStage::AfterPostProcessing, Spot::RenderStage::Overlay}) {
        scene.GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
            stage, [&calls, stage](const Spot::RenderStageContext& context) {
                CHECK(context.stage == stage);
                CHECK((context.depth != nullptr) == (stage < Spot::RenderStage::AfterPostProcessing));
                calls.push_back(std::to_string(static_cast<int>(stage)));
            });
    }
    scene.GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
        Spot::RenderStage::AfterOpaque,
        [&](const Spot::RenderStageContext& stage) { fill(stage, Spot::Color::Green); });
    const auto overlay = scene.GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
        Spot::RenderStage::Overlay, [&](const Spot::RenderStageContext& stage) { fill(stage, Spot::Color::Blue); });
    scene.GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
        Spot::RenderStage::Overlay, [](const Spot::RenderStageContext&) { throw std::runtime_error("Broken hook"); });

    auto& hooks = scene.GetOrAddSystem<Spot::RenderHookSystem>();
    std::vector<std::string> mutations;
    auto self = Spot::RenderHookId::Invalid;
    auto removed = Spot::RenderHookId::Invalid;
    self = hooks.AddRenderHook(Spot::RenderStage::Overlay, [&](const Spot::RenderStageContext&) {
        mutations.push_back("self");
        hooks.RemoveRenderHook(self);
        hooks.RemoveRenderHook(removed);
        hooks.AddRenderHook(Spot::RenderStage::Overlay,
                            [&](const Spot::RenderStageContext&) { mutations.push_back("added"); });
    });
    removed = hooks.AddRenderHook(Spot::RenderStage::Overlay,
                                  [&](const Spot::RenderStageContext&) { mutations.push_back("removed"); });
    hooks.AddRenderHook(Spot::RenderStage::Overlay,
                        [&](const Spot::RenderStageContext&) { mutations.push_back("survivor"); });

    std::vector<std::uint8_t> first;
    std::vector<std::uint8_t> second;
    int frame = 0;
    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        ++frame;
        if (frame == 1) {
            target = device.CreateTexture({.width = 4, .height = 4, .renderTarget = true});
            renderer = std::make_unique<Spot::SceneRenderer>(
                device, Spot::SceneRendererConfig{.targetFormat = Spot::TextureFormat::Rgba8,
                                                  .tonemapping = {.op = Spot::TonemapOperator::Linear, .dither = false},
                                                  .bloom = {.intensity = 0.0f}});
            hdrSolid = Spot::FullscreenPass::Create(
                device, Shaders::SolidFrag,
                {.targetFormat = Spot::TextureFormat::Rgba16Float, .sampleCount = renderer->GetSampleCount()});
            outputSolid = Spot::FullscreenPass::Create(device, Shaders::SolidFrag);
            REQUIRE((target && renderer->IsValid() && hdrSolid && outputSolid));
            renderer->AddHook(Spot::RenderStage::AfterOpaque,
                              [&calls](const Spot::RenderStageContext&) { calls.push_back("renderer"); });
        } else if (frame == 2) {
            first = device.DownloadTexture(*target);
            scene.GetOrAddSystem<Spot::RenderHookSystem>().RemoveRenderHook(overlay);
            calls.clear();
        } else {
            second = device.DownloadTexture(*target);
            return;
        }
        renderer->Render(context, scene, target.get());
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);

    CHECK(mutations == std::vector<std::string>{"self", "survivor", "survivor", "added"});
    CHECK_FALSE(hooks.IsFaulted());
    CHECK(calls == std::vector<std::string>{"0", "renderer", "1", "2", "3", "4", "5"});
    REQUIRE(first.size() == 4 * 4 * 4);
    REQUIRE(second.size() == 4 * 4 * 4);
    // The overlay covers what the hook drew after the opaque stage, until it is removed.
    CHECK(IsNear(FirstPixel(first), {0, 0, 255, 255}));
    CHECK(IsNear(FirstPixel(second), {0, 255, 0, 255}));
    // The hook that threw was logged and removed, once.
    const auto& records = capture->GetRecords();
    CHECK(std::ranges::count_if(records, [](const auto& record) {
              return record.message.find("Broken hook") != std::string::npos;
          }) == 1);
}

TEST_CASE("Instanced particles blend sort flipbook and fade against camera depth offscreen",
          "[gpu][particles][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    auto target =
        device->CreateTexture({.width = 64, .height = 64, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    auto depth = device->CreateTexture(
        {.width = 64, .height = 64, .format = Spot::TextureFormat::Depth32Float, .renderTarget = true});
    REQUIRE(target);
    REQUIRE(depth);
    Spot::ParticleRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8});
    REQUIRE(renderer.IsValid());
    Spot::Camera3D camera;
    camera.orthographic = true;
    camera.orthographicSize = 1;
    camera.nearPlane = 0.1f;
    camera.farPlane = 10.1f;
    Spot::ParticleDrawSettings settings;

    Spot::Particle p;
    p.position = {0, 0, -5.1f};
    p.size = 1;
    p.color = Spot::Color::Red;
    auto render = [&](std::span<const Spot::Particle> particles, float clearDepth,
                      const Spot::Texture* texture = nullptr) {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        context->BeginRenderPass({.depthTarget = depth.get(), .clearDepth = clearDepth, .depthOnly = true}).End();
        context->BeginRenderPass({.clearColor = Spot::Color::Black}).End();
        renderer.Begin(*context, {}, camera, {64, 64}, depth.get());
        renderer.Draw(particles, settings, {}, texture);
        renderer.End();
        device->EndFrame();
        return device->DownloadTexture(*target);
    };
    const auto red = render(std::span(&p, 1), 1);
    const auto center = (32 * 64 + 32) * 4;
    CHECK(red[center] > 240);
    CHECK(red[center + 2] == 0);
    CHECK(red[0] == 0);
    CHECK(renderer.GetStats().particles == 1);
    CHECK(renderer.GetStats().drawCalls == 1);
    const auto hidden = render(std::span(&p, 1), 0.49f);
    CHECK(hidden[center] == 0);
    settings.softDistance = 1;
    const auto soft = render(std::span(&p, 1), 0.55f);
    CHECK(soft[center] > 115);
    CHECK(soft[center] < 140);
    settings.softDistance = 0;
    Spot::Particle far = p;
    far.position.z = -7;
    far.color = {0, 0, 1, 0.5f};
    p.color.a = 0.5f;
    const std::array particles{p, far};
    const auto blended = render(particles, 1);
    CHECK(blended[center] > 120);
    CHECK(blended[center] < 135);
    CHECK(blended[center + 2] > 58);
    CHECK(blended[center + 2] < 70);
    CHECK(renderer.GetStats().drawCalls == 1);
    settings.blend = Spot::ParticleDrawBlend::Additive;
    const std::array glow{p, p};
    const auto additive = render(glow, 1);
    CHECK(additive[center] > 245);
    auto atlas = device->CreateTexture({.width = 2, .height = 1});
    REQUIRE(atlas);
    device->UploadToTexture(*atlas, std::array<std::uint8_t, 8>{255, 0, 0, 255, 0, 0, 255, 255});
    settings.blend = Spot::ParticleDrawBlend::Alpha;
    settings.flipbookColumns = 2;
    p.color = Spot::Color::White;
    p.age = 0.75f;
    p.lifetime = 1;
    const auto flipbook = render(std::span(&p, 1), 1, atlas.get());
    CHECK(flipbook[center + 2] > 240);
    CHECK(flipbook[center] < 15);
}

TEST_CASE("Particles use opaque and cutout camera depth with multisampling offscreen", "[gpu][particles][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    const auto samples = device->SupportsSampleCount(Spot::TextureFormat::Rgba8, 4) &&
                                 device->SupportsSampleCount(Spot::TextureFormat::Depth32Float, 4)
                             ? 4u
                             : 1u;
    auto target =
        device->CreateTexture({.width = 64, .height = 64, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    auto color = device->CreateTexture({.width = 64,
                                        .height = 64,
                                        .format = Spot::TextureFormat::Rgba8,
                                        .renderTarget = true,
                                        .sampleCount = samples});
    auto depth = device->CreateTexture({.width = 64,
                                        .height = 64,
                                        .format = Spot::TextureFormat::Depth32Float,
                                        .renderTarget = true,
                                        .sampleCount = samples});
    REQUIRE(target);
    REQUIRE(color);
    REQUIRE(depth);
    auto cube = Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreateCube());
    REQUIRE(cube);
    Spot::Renderer3D meshes(*device, {.targetFormat = Spot::TextureFormat::Rgba8,
                                      .sampleCount = samples,
                                      .shadowMapSize = 0,
                                      .localShadowMapSize = 0});
    REQUIRE(meshes.IsValid());
    Spot::ParticleRenderer particles(*device, {.targetFormat = Spot::TextureFormat::Rgba8, .sampleCount = samples});
    REQUIRE(particles.IsValid());
    Spot::Camera3D camera;
    camera.orthographic = true;
    camera.orthographicSize = 1;
    Spot::ParticleDrawSettings settings;
    Spot::Particle p;
    p.position = {0, 0, -5};
    p.size = 1;
    p.color = Spot::Color::Red;
    Spot::Material material;
    material.baseColor = Spot::Color::Black;
    auto render = [&] {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        Spot::RenderPassDesc pass{.target = color.get(),
                                  .clearColor = Spot::Color::Black,
                                  .depthTarget = depth.get(),
                                  .clearDepth = 1.0f,
                                  .resolveTarget = samples > 1 ? target.get() : nullptr,
                                  .keepSamples = samples > 1};
        meshes.Begin(*context, pass, camera);
        meshes.DrawMesh(*cube, material, Spot::Matrix4::Translation({0, 0, -4}), false);
        meshes.End();
        const auto* readable = meshes.RenderCameraDepth(*context);
        REQUIRE(readable);
        CHECK(readable->GetSampleCount() == 1);
        particles.Begin(*context, pass, camera, {64, 64}, readable);
        particles.Draw(std::span(&p, 1), settings);
        particles.End();
        device->EndFrame();
        return device->DownloadTexture(samples > 1 ? *target : *color);
    };
    const auto opaque = render();
    const auto center = (32 * 64 + 32) * 4;
    CHECK(opaque[center] == 0);
    material.alphaMode = Spot::AlphaMode::Cutout;
    material.baseColor.a = 0;
    const auto cutout = render();
    CHECK(cutout[center] > 240);
    material.alphaMode = Spot::AlphaMode::Transparent;
    const auto transparent = render();
    CHECK(transparent[center] > 240);
}

TEST_CASE("SDF text scales and world text renders offscreen", "[gpu][text][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    auto target = device->CreateTexture(
        {.width = 256, .height = 128, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    REQUIRE(target);
    Spot::AssetRef<Spot::Font> font(
        Spot::Font::Load(*device, std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "fonts/FiraSans-Regular.ttf", 32));
    REQUIRE(font);
    Spot::Renderer2D renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8});
    REQUIRE(renderer.IsValid());
    auto draw = [&](float scale, bool world) {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
        renderer.Begin(pass, Spot::UiSystem::ScreenCamera({256, 128}), {256, 128});
        renderer.DrawQuad({240, 10}, {8, 8}, Spot::Color::Blue);
        if (world) {
            Spot::Scene scene;
            auto& entity = scene.CreateEntity("Label");
            entity.GetTransform().SetPosition({20, 110, 0});
            auto& text = entity.AddComponent<Spot::TextRenderer>();
            text.SetFont(font);
            text.SetText("A");
            text.SetPixelScale(scale);
            text.SetFacingCamera(true);
            Spot::DrawWorldText(renderer, scene, {});
        } else {
            renderer.DrawString(*font, "A", {20, 110}, Spot::Color::White, scale);
        }
        renderer.DrawQuad({230, 10}, {8, 8}, Spot::Color::Red);
        renderer.End();
        CHECK(renderer.GetStats().quadCount == 3);
        pass.End();
        device->EndFrame();
        return device->DownloadTexture(*target);
    };
    const auto small = draw(1, false);
    const auto large = draw(3, false);
    const auto world = draw(3, true);
    auto brightPixels = [](const auto& pixels) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i] > 200 && pixels[i + 1] > 200 && pixels[i + 2] > 200)
                ++count;
        }
        return count;
    };
    CHECK(brightPixels(small) > 20);
    CHECK(brightPixels(large) > brightPixels(small) * 5);
    CHECK(world == large);
}

TEST_CASE("Authored UI previews and scroll clipping render offscreen", "[gpu][ui][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    auto target =
        device->CreateTexture({.width = 64, .height = 64, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    REQUIRE(target);
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& panel = root.CreateChild("Scroll");
    auto& rect = panel.AddComponent<Spot::UiRect>();
    rect.layout.anchorMin = rect.layout.anchorMax = rect.layout.pivot = {};
    rect.layout.offset = {8, 8};
    rect.layout.sizeDelta = {32, 32};
    panel.AddComponent<Spot::UiScrollView>().contentSize = {64, 64};
    auto& child = panel.CreateChild("Clipped Image");
    auto& childRect = child.AddComponent<Spot::UiRect>();
    childRect.layout.anchorMin = {};
    childRect.layout.anchorMax = {1, 1};
    childRect.layout.sizeDelta = {};
    child.AddComponent<Spot::UiImage>().tint = Spot::Color::Red;
    Spot::Renderer2D renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8});
    auto drawFrame = [&] {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
        renderer.Begin(pass, Spot::UiSystem::ScreenCamera({64, 64}), {64, 64});
        scene.GetOrAddSystem<Spot::UiSystem>().DrawUi(renderer, {64, 64});
        renderer.DrawQuad({60, 4}, {4, 4}, Spot::Color::Blue);
        renderer.End();
        pass.End();
        device->EndFrame();
        return device->DownloadTexture(*target);
    };
    const auto pixels = drawFrame();
    CHECK(drawFrame() == pixels);
    auto red = [&](int x, int y) { return pixels[static_cast<std::size_t>((y * 64 + x) * 4)]; };
    CHECK(red(20, 40) > 240);
    CHECK(red(7, 40) == 0);
    CHECK(red(40, 40) == 0);
    CHECK(red(20, 23) == 0);
    CHECK(red(20, 56) == 0);
    CHECK(pixels[(60 * 64 + 60) * 4 + 2] > 240);
}

#ifdef SPOT_SANDBOX_IMPORTED_DIR
TEST_CASE("The generated humanoid renders animated poses, a held pistol and ragdolls",
          "[gpu][animation][sandbox][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SPOT_SANDBOX_IMPORTED_DIR, device.get());
    struct AssetScope {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~AssetScope() { Spot::Detail::SetAssetManager(previous); }
    } scope;
    Spot::Detail::SetAssetManager(&assets);
    const auto prefab = assets.Load<Spot::Prefab>("prefabs/actors/npc.prefab");
    const auto weapon = assets.Load<Spot::Model>("generated/models/pistol.gltf");
    REQUIRE(prefab);
    REQUIRE(weapon);
    Spot::Scene scene;
    auto* npc = scene.Instantiate(prefab);
    REQUIRE(npc);
    auto& floor = scene.CreateEntity("Floor");
    floor.GetTransform().SetPosition({0, -0.95f, 0});
    floor.AddComponent<Spot::BoxCollider>(Spot::Vector3{10, 0.1f, 10});
    floor.AddComponent<Spot::MeshRenderer>(Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreatePlane(10)),
                                           Spot::Material{.baseColor = {0.3f, 0.3f, 0.3f, 1}});
    auto& cameraEntity = scene.CreateEntity("Camera");
    cameraEntity.GetTransform().SetPosition({2.8f, 1.5f, -4});
    cameraEntity.GetTransform().SetRotation(Spot::Quaternion::LookRotation(-cameraEntity.GetTransform().GetPosition()));
    cameraEntity.AddComponent<Spot::Camera>().SetBackground({0.08f, 0.09f, 0.12f, 1});
    auto& light = scene.CreateEntity("Light");
    light.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.5f, -1, 0.5f}));
    light.AddComponent<Spot::DirectionalLight>();
    scene.CreateEntity("Ambient").AddComponent<Spot::AmbientLight>();
    auto target = device->CreateTexture(
        {.width = 640, .height = 480, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb,
                                           .sampleCount = 1,
                                           .shadowMapSize = 512,
                                           .localShadowMapSize = 0});
    const auto capture = Spot::Process::GetEnvironmentValue("SPOT_ANIMATION_CAPTURE_DIR");
    const auto render = [&](std::string_view name) {
        scene.PrepareToRender();
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Render(*context, scene, target.get());
        device->EndFrame();
        auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == 640 * 480 * 4);
        if (capture && !capture->empty()) {
            std::filesystem::create_directories(*capture);
            CHECK(
                Spot::Image(640, 480, pixels).SavePng(std::filesystem::path(*capture) / (std::string(name) + ".png")));
        }
        return pixels;
    };
    const auto changed = [](const auto& a, const auto& b) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            count += std::abs(static_cast<int>(a[i]) - b[i]) > 10 ? 1 : 0;
        }
        return count;
    };
    // The pistol follows the hand through the authored holding pose and locomotion.
    auto& pistol = scene.Instantiate(*weapon, "Pistol");
    auto& attachment = pistol.AddComponent<Spot::BoneAttachment>();
    attachment.SetAnimator(npc->GetId());
    attachment.SetBone("RightHand");
    scene.Update(0);
    auto* animator = npc->GetComponent<Spot::Animator>();
    REQUIRE(animator);
    REQUIRE(animator->SetLayerWeight("Holding", 1));
    REQUIRE(animator->SetFloat("speed", 0));
    animator->Seek(0);
    animator->RefreshRenderers();
    attachment.Synchronize();
    const auto held = pistol.GetTransform().GetWorldPosition();
    const auto idle = render("humanoid_armed_idle");
    REQUIRE(animator->SetFloat("speed", 2));
    animator->Seek(0.15f);
    animator->RefreshRenderers();
    attachment.Synchronize();
    CHECK(Spot::Distance(pistol.GetTransform().GetWorldPosition(), held) > 0.01f);
    const auto run = render("humanoid_armed_run");
    CHECK(changed(idle, run) > 500);
    auto* ragdoll = npc->GetComponent<Spot::Ragdoll>();
    REQUIRE(ragdoll);
    REQUIRE(ragdoll->SetActive(true));
    ragdoll->AddImpulse({10, 0, 10}, {0, 0.5f, 0});
    for (int i = 0; i < 90; ++i) {
        scene.Update(1.0f / 60);
    }
    const auto fallen = render("humanoid_ragdoll");
    CHECK(changed(run, fallen) > 500);
}

TEST_CASE("Spawn menu thumbnails render real prefab meshes and retain their cached textures",
          "[gpu][ui][sandbox][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SPOT_SANDBOX_IMPORTED_DIR, device.get());
    struct AssetScope {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~AssetScope() { Spot::Detail::SetAssetManager(previous); }
    } scope;
    Spot::Detail::SetAssetManager(&assets);
    const auto prefab = assets.Load<Spot::Prefab>("prefabs/spawnables/props/crate.prefab");
    REQUIRE(prefab);
    Spot::Scene interfaceScene;
    auto& entity = interfaceScene.CreateEntity("Thumbnail");
    auto& image = entity.AddComponent<Spot::UiImage>();
    MenuThumbnails thumbnails;
    thumbnails.Add(entity.GetId(), prefab);
    auto target = device->CreateTexture(
        {.width = 128, .height = 128, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    REQUIRE(target);
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    Spot::RenderStageContext stage{.device = *device,
                                   .context = *context,
                                   .stage = Spot::RenderStage::BeforeOpaque,
                                   .camera = {},
                                   .viewProjection = {},
                                   .viewSize = {128, 128},
                                   .pass = {},
                                   .colorFormat = Spot::TextureFormat::Rgba8,
                                   .sampleCount = 1,
                                   .depth = nullptr,
                                   .sceneColor = nullptr};
    thumbnails.Render(stage, interfaceScene);
    REQUIRE(image.texture);
    CHECK(thumbnails.GetCompletedCount() == 1);
    const auto* cached = image.texture.Get();
    thumbnails.Render(stage, interfaceScene);
    CHECK(image.texture.Get() == cached);
    CHECK(thumbnails.GetCompletedCount() == 1);
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*image.texture);
    REQUIRE(pixels.size() == 128 * 128 * 4);
    std::size_t different = 0;
    for (std::size_t index = 0; index < pixels.size(); index += 4) {
        if (std::abs(static_cast<int>(pixels[index]) - pixels[0]) > 8 ||
            std::abs(static_cast<int>(pixels[index + 1]) - pixels[1]) > 8)
            ++different;
    }
    CHECK(different > 500);
}

#endif

TEST_CASE("GPU skinning matches transformed meshes in color, shadows and camera depth", "[gpu][animation][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    auto data = Spot::PrimitiveMeshes::CreateCube(0.8f);
    data.skin.resize(data.vertices.size());
    for (auto& skin : data.skin) {
        skin.joints = {0, 1, 0, 0};
        skin.weights = {0.25f, 0.75f, 0, 0};
    }
    auto mesh = Spot::Mesh::Create(*device, data);
    auto floor = Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreatePlane(8));
    REQUIRE(mesh);
    REQUIRE(floor);
    REQUIRE(mesh->GetSkinBuffer());
    CHECK(mesh->GetSkinJointCount() == 2);
    const std::array palette{Spot::Matrix4{}, Spot::Matrix4::Translation({1.2f, 0, 0})};
    Spot::Camera3D camera{.position = {0, 3, 6}};
    camera.LookAt({0, 0, 0});
    for (const auto samples : {1u, 4u}) {
        auto target = device->CreateTexture(
            {.width = 96, .height = 96, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
        auto color = samples == 1 ? nullptr
                                  : device->CreateTexture({.width = 96,
                                                           .height = 96,
                                                           .format = Spot::TextureFormat::Rgba8,
                                                           .renderTarget = true,
                                                           .sampleCount = samples});
        auto depth = device->CreateTexture({.width = 96,
                                            .height = 96,
                                            .format = Spot::TextureFormat::Depth32Float,
                                            .renderTarget = true,
                                            .sampleCount = samples});
        REQUIRE(target);
        REQUIRE(depth);
        Spot::Renderer3D renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8,
                                            .sampleCount = samples,
                                            .shadowMapSize = 128,
                                            .shadowCascadeCount = 1,
                                            .localShadowMapSize = 0});
        REQUIRE(renderer.IsValid());
        for (const auto mode : {Spot::AlphaMode::Opaque, Spot::AlphaMode::Cutout, Spot::AlphaMode::Transparent}) {
            CAPTURE(samples, static_cast<int>(mode));
            Spot::Material material{
                .baseColor = {0.8f, 0.2f, 0.1f, 1}, .metallic = 0, .alphaMode = mode, .doubleSided = true};
            const auto render = [&](bool skinned, bool moved) {
                auto* context = device->BeginOffscreenFrame(*target);
                REQUIRE(context);
                renderer.Begin(*context,
                               {.target = color ? color.get() : target.get(),
                                .clearColor = Spot::Color::Black,
                                .depthTarget = depth.get(),
                                .clearDepth = 1.0f,
                                .resolveTarget = color ? target.get() : nullptr},
                               camera);
                renderer.DrawLight({.type = Spot::LightType::Ambient, .intensity = 0.15f});
                renderer.DrawLight(
                    {.type = Spot::LightType::Directional, .direction = {-0.5f, -1, -0.2f}, .castsShadows = true});
                renderer.DrawMesh(*floor, Spot::Material{.metallic = 0}, Spot::Matrix4::Translation({0, -0.5f, 0}));
                if (skinned) {
                    renderer.DrawSkinnedMesh(*mesh, material, palette);
                } else {
                    renderer.DrawMesh(*mesh, material, Spot::Matrix4::Translation({moved ? 0.9f : 0.0f, 0, 0}));
                }
                renderer.End();
                REQUIRE(renderer.RenderCameraDepth(*context));
                device->EndFrame();
                return device->DownloadTexture(*target);
            };
            const auto reference = render(false, true), animated = render(true, true), bind = render(false, false);
            REQUIRE(animated.size() == reference.size());
            std::size_t mismatches = 0, changed = 0;
            for (std::size_t i = 0; i < animated.size(); ++i) {
                mismatches += std::abs(static_cast<int>(animated[i]) - reference[i]) > 3 ? 1 : 0;
                changed += std::abs(static_cast<int>(animated[i]) - bind[i]) > 10 ? 1 : 0;
            }
            CHECK(mismatches < animated.size() / 100);
            CHECK(changed > 80);
            CHECK(renderer.GetStats().shadowDrawCalls > 0);
        }
    }
}

TEST_CASE("Renderer3D draws clouds over the day sky, and stars and the moon at night", "[gpu]") {
    RequireGpu();
    const auto render = [](const Spot::SkyData& sky, Spot::Vector3 forward) {
        Spot::Camera3D camera;
        camera.rotation = Spot::Quaternion::LookRotation(Spot::Normalize(forward), {0.0f, 0.0f, -1.0f});
        return TargetRender(
            256, 256,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                REQUIRE(renderer.IsValid());
                renderer.Begin(context, target, camera);
                renderer.DrawSky(sky);
                renderer.End();
            });
    };
    // How saturated the sky is on average, from 0, gray, to 1.
    const auto saturation = [](const TargetRender& image) {
        double total = 0.0;
        for (int y = 0; y < 256; y += 4) {
            for (int x = 0; x < 256; x += 4) {
                const Pixel pixel = image.At(x, y);
                const int high = std::max({int{pixel[0]}, int{pixel[1]}, int{pixel[2]}});
                const int low = std::min({int{pixel[0]}, int{pixel[1]}, int{pixel[2]}});
                total += high > 0 ? static_cast<double>(high - low) / high : 0.0;
            }
        }
        return total / (64.0 * 64.0);
    };

    // White clouds take the blue out of a clear sky.
    const Spot::SkyData day{.sunDirection = {-0.3f, -1.0f, -0.2f}};
    Spot::SkyData cloudy = day;
    cloudy.clouds.coverage = 0.8f;
    const Spot::Vector3 up{0.1f, 1.0f, 0.05f};
    const double clear = saturation(render(day, up));
    const double clouded = saturation(render(cloudy, up));
    INFO("Saturation " << clear << " clear, " << clouded << " cloudy");
    CHECK(clouded < clear * 0.8);

    // At night, stars come out, and the moon rises opposite the sun.
    const Spot::SkyData night{.sunDirection = {0.3f, 1.0f, 0.2f}};
    Spot::SkyData dark = night;
    dark.night.starBrightness = 0.0f;
    dark.night.moonSize = 0.0f;
    const auto starry = render(night, up);
    const auto starless = render(dark, up);
    int stars = 0;
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            stars += starry.At(x, y)[1] > starless.At(x, y)[1] + 25 ? 1 : 0;
        }
    }
    CHECK(stars > 20);
    const Spot::Vector3 toMoon{0.3f, 1.0f, 0.2f};
    const Pixel moon = render(night, toMoon).At(128, 128);
    const Pixel moonless = render(dark, toMoon).At(128, 128);
    INFO("Moon " << int{moon[0]} << ", " << int{moon[1]} << ", " << int{moon[2]});
    CHECK(moon[1] > moonless[1] + 80);
}

TEST_CASE("Renderer3D draws clouds where they are in the world, under what is nearer and over what is beyond",
          "[gpu]") {
    RequireGpu();
    constexpr int Size = 64;
    // A block far wider than the view and ten meters thick, its top at the height.
    struct Block {
        float height;
        Spot::Color color;
    };
    const auto render = [](const Spot::SkyData& sky, Spot::Vector3 position, Spot::Vector3 forward,
                           std::optional<Block> block = std::nullopt, std::uint32_t samples = 1) {
        Spot::Camera3D camera;
        camera.position = position;
        camera.rotation = Spot::Quaternion::LookRotation(Spot::Normalize(forward), {0.0f, 0.0f, -1.0f});
        camera.farPlane = 10000.0f;
        return TargetRender(
            Size, Size,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8, .sampleCount = samples});
                REQUIRE(renderer.IsValid());
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                REQUIRE(cube);
                renderer.Begin(context, target, camera);
                renderer.DrawSky(sky);
                if (block) {
                    renderer.DrawMesh(*cube, {.baseColor = block->color},
                                      Spot::Matrix4::Translation({0.0f, block->height - 5.0f, 0.0f}) *
                                          Spot::Matrix4::Scale({20000.0f, 10.0f, 20000.0f}));
                }
                renderer.End();
            },
            {}, samples);
    };
    // How many pixels of the two differ by more than a few steps.
    const auto differing = [](const TargetRender& a, const TargetRender& b) {
        int count = 0;
        for (int y = 0; y < Size; ++y) {
            for (int x = 0; x < Size; ++x) {
                count += IsNear(a.At(x, y), b.At(x, y)) ? 0 : 1;
            }
        }
        return count;
    };

    const Spot::SkyData clear{.sunDirection = {-0.3f, -1.0f, -0.2f}};
    Spot::SkyData cloudy = clear;
    // Small enough for several to be in view.
    cloudy.clouds = {.coverage = 0.5f, .density = 1.0f, .height = 1500.0f, .size = 300.0f};
    const Spot::Vector3 up{0.1f, 1.0f, 0.05f};

    // They stay where they are as the camera moves under them, and the wind carries them along its direction.
    const auto here = render(cloudy, {}, up);
    const auto moved = render(cloudy, {600.0f, 0.0f, 0.0f}, up);
    Spot::SkyData carried = cloudy;
    carried.clouds.offset = {600.0f, 0.0f};
    const auto followed = render(carried, {600.0f, 0.0f, 0.0f}, up);
    CHECK(differing(here, render(clear, {}, up)) > Size * Size / 4);
    CHECK(differing(here, moved) > Size * Size / 10);
    CHECK(differing(here, followed) < Size * Size / 100);

    // A roof between the camera and the clouds hides them.
    const Block roof{200.0f, Spot::Color::Red};
    CHECK(differing(render(cloudy, {}, up, roof), render(clear, {}, up, roof)) == 0);

    // Seen from above, they hide the ground under them, multisampled or not.
    const Spot::Vector3 down{0.1f, -1.0f, 0.05f};
    const Block ground{0.0f, {0.1f, 0.3f, 0.1f}};
    for (const std::uint32_t samples : {1u, 4u}) {
        CAPTURE(samples);
        const auto over = render(cloudy, {0.0f, 3000.0f, 0.0f}, down, ground, samples);
        const auto bare = render(clear, {0.0f, 3000.0f, 0.0f}, down, ground, samples);
        const int covered = differing(over, bare);
        CHECK(covered > Size * Size / 4);
        // Lit from above, they are brighter than the ground; between them, it lies in their shadows.
        int brighter = 0;
        for (int y = 0; y < Size; ++y) {
            for (int x = 0; x < Size; ++x) {
                brighter += over.At(x, y)[1] > bare.At(x, y)[1] + 20 ? 1 : 0;
            }
        }
        CHECK(brighter > Size * Size / 8);
    }
}

TEST_CASE("Renderer3D shades the ground under clouds from the sun", "[gpu]") {
    RequireGpu();
    const auto brightness = [](const Spot::SkyData& sky) {
        Spot::Camera3D camera;
        camera.position = {0.0f, 10.0f, 0.0f};
        camera.rotation = Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.01f}, {0.0f, 0.0f, -1.0f});
        const TargetRender render(
            32, 32,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(device, {.targetFormat = Spot::TextureFormat::Rgba8});
                REQUIRE(renderer.IsValid());
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                REQUIRE(cube);
                renderer.Begin(context, target, camera);
                renderer.DrawSky(sky);
                renderer.DrawMesh(*cube, {.baseColor = {0.5f, 0.5f, 0.5f}},
                                  Spot::Matrix4::Translation({0.0f, -5.0f, 0.0f}) *
                                      Spot::Matrix4::Scale({1000.0f, 10.0f, 1000.0f}));
                renderer.End();
            });
        double total = 0.0;
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                total += render.At(x, y)[1];
            }
        }
        return total / (32.0 * 32.0);
    };
    const Spot::SkyData clear{.sunDirection = {-0.3f, -1.0f, -0.2f}};
    Spot::SkyData overcast = clear;
    overcast.clouds = {.coverage = 1.0f, .density = 1.0f};
    const double sunlit = brightness(clear);
    const double shaded = brightness(overcast);
    INFO("Sunlit " << sunlit << ", shaded " << shaded);
    CHECK(shaded < sunlit * 0.7);
    CHECK(shaded > 5.0);
}

TEST_CASE("Renderer3D draws clouds as a volume to fly through, hidden by what is in front of them", "[gpu]") {
    RequireGpu();
    constexpr int Size = 64;
    const auto render = [](const Spot::SkyData& sky, Spot::Vector3 position, Spot::Vector3 forward, bool roof = false) {
        Spot::Camera3D camera;
        camera.position = position;
        camera.rotation = Spot::Quaternion::LookRotation(Spot::Normalize(forward), {0.0f, 0.0f, -1.0f});
        camera.farPlane = 10000.0f;
        return TargetRender(
            Size, Size,
            [&](Spot::GraphicsDevice& device, Spot::RenderContext& context, const Spot::RenderPassDesc& target) {
                Spot::Renderer3D renderer(
                    device, {.targetFormat = Spot::TextureFormat::Rgba8, .hdr = true, .volumetricClouds = true});
                REQUIRE(renderer.IsValid());
                const auto cube = Spot::Mesh::Create(device, Spot::PrimitiveMeshes::CreateCube());
                REQUIRE(cube);
                renderer.Begin(context, target, camera);
                renderer.DrawSky(sky);
                if (roof) {
                    renderer.DrawMesh(*cube, {.baseColor = Spot::Color::Red},
                                      Spot::Matrix4::Translation({0.0f, 195.0f, 0.0f}) *
                                          Spot::Matrix4::Scale({20000.0f, 10.0f, 20000.0f}));
                }
                renderer.End();
            });
    };
    const auto differing = [](const TargetRender& a, const TargetRender& b) {
        int count = 0;
        for (int y = 0; y < Size; ++y) {
            for (int x = 0; x < Size; ++x) {
                count += IsNear(a.At(x, y), b.At(x, y)) ? 0 : 1;
            }
        }
        return count;
    };

    const Spot::SkyData clear{.sunDirection = {-0.3f, -1.0f, -0.2f}};
    Spot::SkyData cloudy = clear;
    cloudy.clouds = {.coverage = 0.6f, .density = 1.0f, .height = 1500.0f, .size = 400.0f, .thickness = 800.0f};
    const Spot::Vector3 up{0.1f, 1.0f, 0.05f};

    // From below, they cover much of the sky, unless a roof is in front of them.
    CHECK(differing(render(cloudy, {}, up), render(clear, {}, up)) > Size * Size / 4);
    CHECK(differing(render(cloudy, {}, up, true), render(clear, {}, up, true)) == 0);

    // Inside an overcast sky, the camera sees cloud all around.
    Spot::SkyData overcast = cloudy;
    overcast.clouds.coverage = 1.0f;
    const Spot::Vector3 inside{0.0f, 1800.0f, 0.0f};
    const Spot::Vector3 level{1.0f, 0.02f, 0.3f};
    CHECK(differing(render(overcast, inside, level), render(clear, inside, level)) > Size * Size * 9 / 10);
}

TEST_CASE("Clouds are captured in fixed reference views, to compare changes to them against", "[gpu][clouds]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({});
    REQUIRE(device);
    // The same sky, weather and clouds every time, still, as the wind does not blow while nothing updates them.
    Spot::Scene scene;
    auto& sun = scene.CreateEntity("Sun");
    sun.AddComponent<Spot::DirectionalLight>();
    auto& sky = scene.CreateEntity("Sky");
    sky.AddComponent<Spot::Sky>();
    auto& clouds = sky.AddComponent<Spot::Clouds>();
    clouds.coverage = 0.5f;
    clouds.density = 0.85f;
    clouds.height = 1500.0f;
    clouds.size = 1000.0f;
    clouds.thickness = 1500.0f;
    clouds.softness = 0.35f;
    clouds.windSpeed = 0.0f;
    auto& camera = scene.CreateEntity("Camera");
    camera.AddComponent<Spot::Camera>().SetFarPlane(20000.0f);
    auto target = device->CreateTexture(
        {.width = 960, .height = 540, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());

    struct View {
        const char* name;
        Spot::Vector3 position;
        Spot::Vector3 target;
        Spot::Vector3 toSun;
    };
    const std::array views{
        View{"below_noon.png", {0, 2, 0}, {300, 900, -1000}, {0.3f, 0.9f, 0.3f}},
        View{"below_toward_sun.png", {0, 2, 0}, {-1000, 450, -300}, {-0.9f, 0.35f, -0.2f}},
        View{"sunset.png", {0, 2, 0}, {-1000, 250, 300}, {-0.95f, 0.06f, 0.25f}},
        View{"horizon.png", {0, 2, 0}, {1000, 60, -200}, {0.4f, 0.6f, 0.6f}},
        View{"above.png", {0, 3800, 0}, {1000, 3300, -1000}, {0.3f, 0.8f, 0.4f}},
        View{"inside.png", {0, 2100, 0}, {1000, 2120, -400}, {0.3f, 0.8f, 0.4f}},
    };
    const std::filesystem::path output(SPOT_CLOUD_REFERENCE_DIR);
    std::filesystem::create_directories(output);
    for (const View& view : views) {
        CAPTURE(view.name);
        sun.GetTransform().SetRotation(Spot::Quaternion::LookRotation(-Spot::Normalize(view.toSun)));
        camera.GetTransform().SetPosition(view.position);
        camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation(view.target - view.position));
        scene.Update(0.0f);
        // Enough frames for the volume's blend over frames to even out its noise, as it does while a game runs.
        for (int frame = 0; frame < 12; ++frame) {
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, scene, target.get());
            device->EndFrame();
        }
        const auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == 960 * 540 * 4);
        CHECK(Spot::Image(960, 540, pixels).SavePng(output / view.name));
    }
}

TEST_CASE("Every engine renderer is made and draws on a device with only the baseline", "[gpu][baseline]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.baselineOnly = true, .debug = false});
    REQUIRE(device);
    const Spot::GraphicsCapabilities& capabilities = device->GetCapabilities();
    REQUIRE_FALSE((capabilities.compute || capabilities.indirectDraws || capabilities.timestamps ||
                   capabilities.bindlessTextures || capabilities.compressedTextures));
    REQUIRE(capabilities.maxColorTargets == 1);

    const Spot::TextureFormat format = Spot::TextureFormat::Rgba8;
    CHECK(Spot::Renderer2D(*device, {.targetFormat = format}).IsValid());
    CHECK(Spot::Renderer3D(*device, {.targetFormat = format}).IsValid());
    CHECK(Spot::LineRenderer(*device, {.targetFormat = format}).IsValid());
    CHECK(Spot::ParticleRenderer(*device, {.targetFormat = format}).IsValid());
    CHECK(Spot::SceneRenderer(*device, {.targetFormat = format}).IsValid());

    auto target = device->CreateTexture({.width = 4, .height = 4, .renderTarget = true});
    REQUIRE(target);
    Spot::Renderer2D renderer(*device, {.targetFormat = format});
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    {
        auto pass = context->BeginRenderPass({.clearColor = Spot::Color::Black});
        renderer.Begin(pass, Spot::Camera2D{}, {4.0f, 4.0f});
        renderer.DrawQuad({0.0f, 0.0f}, {4.0f, 4.0f}, Spot::Color::Green);
        renderer.End();
    }
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*target);
    REQUIRE(pixels.size() == 4 * 4 * 4);
    CHECK(pixels[1] == 255);
}
