#include "rendering_application.h"
#include "temp_directory.h"

#include "app/editor_context.h"
#include "assets/thumbnail_cache.h"
#include "code/vice_editor.h"
#include "panels/code_editor_panel.h"
#include "project/editor_project.h"
#include "scene/entity_actions.h"
#include "scene/scene_document.h"

#include "imgui/asset_icons.h"
#include "imgui/editor_style.h"
#include "imgui/imgui_renderer.h"
#include "viewport/viewport_renderer.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

using Spot::Tests::RenderingApplication;
using Spot::Tests::RequireGpu;

TEST_CASE("The embedded ViceScript editor renders syntax colors and monospaced text", "[editor][gpu][code][vice]") {
    RequireGpu();
    constexpr std::uint32_t Width = 800;
    constexpr std::uint32_t Height = 450;
    TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Code", Spot::ProjectTemplate::Empty);
    REQUIRE(file);
    auto project = Spot::Editor::EditorProject::Open(*file, nullptr);
    REQUIRE(project);
    std::ofstream(project->GetProject().GetAssetsDirectory() / "hello.vice")
        << "// Fields and methods resolve on the current instance\nimport spot\n"
           "class PlayerController : Component {\n    var speed = 3\n"
           "    func OnStart() { entity.name = \"Player\" }\n"
           "    func Move(deltaTime) {\n"
           "        entity.transform.Translate(Vector3(speed * deltaTime, 0, 0))\n    }\n"
           "    func OnUpdate(deltaTime) {\n"
           "        if spot.Input.IsKeyDown(\"Right\") { Move(deltaTime) }\n"
           "    }\n}\n";
    project->Refresh();
    const auto asset = project->GetAssetDatabase().FindByPath("hello.vice");
    REQUIRE(asset);
    ImGuiContext* imgui = ImGui::CreateContext();
    Spot::Editor::LoadEditorFonts();
    Spot::Editor::ApplyEditorStyle(1.0f);
    Spot::Editor::EditorSettings settings;
    std::unique_ptr<Spot::Editor::EditorContext> context;
    std::unique_ptr<Spot::Editor::CodeEditorPanel> panel;
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::Editor::ImGuiRenderer> renderer;
    std::vector<std::uint8_t> pixels;
    int frame = 0;
    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& renderContext) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 3) {
            pixels = device.DownloadTexture(*target);
            return;
        }
        if (frame == 1) {
            context = std::make_unique<Spot::Editor::EditorContext>(app.GetWindow(), nullptr, settings);
            context->SetProject(std::move(project));
            panel = std::make_unique<Spot::Editor::CodeEditorPanel>(*context);
            REQUIRE(panel->OpenScript(*asset));
            target = device.CreateTexture({.width = Width, .height = Height, .renderTarget = true});
            renderer = std::make_unique<Spot::Editor::ImGuiRenderer>(device, Spot::TextureFormat::Rgba8);
            REQUIRE(target);
            REQUIRE(renderer->IsValid());
        }
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {static_cast<float>(Width), static_cast<float>(Height)};
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        if (frame == 2) {
            io.AddMousePosEvent(150, 150);
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddMouseWheelEvent(0, 4);
        }
        ImGui::NewFrame();
        REQUIRE(Spot::Editor::GetCodeFont());
        ImGui::PushFont(Spot::Editor::GetCodeFont(), 14.0f);
        CHECK(ImGui::CalcTextSize("iiii").x == ImGui::CalcTextSize("WWWW").x);
        const float defaultGlyphWidth = ImGui::CalcTextSize("#").x;
        ImGui::PopFont();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({static_cast<float>(Width), static_cast<float>(Height)});
        panel->Draw();
        if (frame == 2) {
            CHECK(panel->GetActiveEditor()->GetGlyphWidth() > defaultGlyphWidth);
        }
        ImGui::Render();
        auto pass = renderContext.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        renderer->Render(pass, *ImGui::GetDrawData());
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);
    renderer.reset();
    panel.reset();
    context.reset();
    ImGui::DestroyContext(imgui);
    REQUIRE(pixels.size() == Width * Height * 4);
    std::size_t purple = 0;
    std::size_t orange = 0;
    std::size_t green = 0;
    std::size_t cyan = 0;
    std::size_t teal = 0;
    for (std::uint32_t y = 90; y < Height - 30; ++y) {
        for (std::uint32_t x = 40; x < Width; ++x) {
            const auto offset = (y * Width + x) * 4;
            const int red = pixels[offset];
            const int g = pixels[offset + 1];
            const int blue = pixels[offset + 2];
            purple += blue > red + 15 && red > g + 20 ? 1 : 0;
            orange += red > g + 20 && g > blue + 20 ? 1 : 0;
            green += g > red + 20 && g > blue + 20 ? 1 : 0;
            cyan += blue > red + 40 && blue > g + 15 ? 1 : 0;
            teal += g > red + 50 && blue > red + 50 && std::abs(g - blue) < 20 ? 1 : 0;
        }
    }
    CHECK(purple > 20);
    CHECK(orange > 20);
    CHECK(green > 20);
    CHECK(cyan > 20);
    CHECK(teal > 20);
    if (const auto capture = Spot::Process::GetEnvironmentValue("SPOT_CODE_CAPTURE")) {
        CHECK(Spot::Image(Width, Height, std::move(pixels)).SavePng(Spot::Utf8ToPath(*capture)));
    }
}

TEST_CASE("ViceScript icons render as code documents beside C++ files", "[editor][gpu][vice]") {
    RequireGpu();
    constexpr std::uint32_t Width = 256;
    constexpr std::uint32_t Height = 136;
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::Editor::ImGuiRenderer> renderer;
    std::vector<std::uint8_t> pixels;
    ImGuiContext* context = ImGui::CreateContext();
    int frame = 0;
    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& renderContext) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            pixels = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1)
            return;
        target = device.CreateTexture({.width = Width, .height = Height, .renderTarget = true});
        renderer = std::make_unique<Spot::Editor::ImGuiRenderer>(device, Spot::TextureFormat::Rgba8);
        REQUIRE(target);
        REQUIRE(renderer->IsValid());
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {static_cast<float>(Width), static_cast<float>(Height)};
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        auto* drawList = ImGui::GetForegroundDrawList();
        Spot::Editor::AssetIcons::DrawFile(drawList, {8, 8}, 112, "component.cpp");
        Spot::Editor::AssetIcons::DrawAsset(drawList, {136, 8}, 112, Spot::AssetType::Script);
        ImGui::Render();
        auto pass = renderContext.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        renderer->Render(pass, *ImGui::GetDrawData());
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);
    renderer.reset();
    ImGui::DestroyContext(context);
    REQUIRE(pixels.size() == Width * Height * 4);
    for (const std::uint32_t start : {0u, 128u}) {
        std::size_t accentPixels = 0;
        for (std::uint32_t y = 0; y < Height; ++y) {
            for (std::uint32_t x = start; x < start + 128; ++x) {
                const auto offset = (y * Width + x) * 4;
                if (pixels[offset + 2] > pixels[offset] + 15 && pixels[offset] > pixels[offset + 1] + 10) {
                    ++accentPixels;
                }
            }
        }
        CHECK(accentPixels > 100);
    }
    if (const auto capture = Spot::Process::GetEnvironmentValue("SPOT_ICON_CAPTURE")) {
        CHECK(Spot::Image(Width, Height, pixels).SavePng(Spot::Utf8ToPath(*capture)));
    }
}

TEST_CASE("Pixel-perfect scene cameras keep pixel edges in both editor and game views",
          "[editor][gpu][pixelperfect][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    auto texture = device->CreateTexture({.width = 2, .height = 1});
    REQUIRE(texture);
    device->UploadToTexture(*texture, std::array<std::uint8_t, 8>{255, 0, 0, 255, 0, 255, 0, 255});
    Spot::Scene scene;
    auto& sprite = scene.CreateEntity();
    sprite.GetTransform().SetPosition({0.1f, 0.5f, 0});
    sprite.AddComponent<Spot::SpriteRenderer>(std::shared_ptr<const Spot::Texture>(std::move(texture)),
                                              Spot::Vector2{2, 1});
    auto& cameraEntity = scene.CreateEntity();
    cameraEntity.GetTransform().SetPosition({0.1f, 0.1f, 3});
    auto& camera = cameraEntity.AddComponent<Spot::Camera>();
    camera.SetProjection(Spot::CameraProjection::Orthographic);
    camera.SetPixelPerfect(true);
    camera.SetPixelsPerUnit(1);
    camera.SetReferenceResolution({32, 24});
    camera.SetBackground(Spot::Color::Black);
    Spot::Editor::ViewportRenderer viewport(*device);
    Spot::Editor::ViewportTargets targets;
    REQUIRE(targets.Resize(*device, 64, 48, viewport.GetSampleCount()));
    Spot::Editor::ViewportOptions options;
    options.showGrid = false;
    options.defaultLights = false;
    options.background = Spot::Color::Black;
    options.fxaa = true;
    options.tonemapping = {.op = Spot::TonemapOperator::Linear, .dither = false};
    options.bloom.intensity = 1;
    options.bloom.threshold = 0;
    auto* context = device->BeginOffscreenFrame(*targets.GetColor());
    REQUIRE(context);
    viewport.Render(*context, targets, scene, camera.GetCamera3D(), options);
    device->EndFrame();
    const auto editor = device->DownloadTexture(*targets.GetColor());
    auto target = device->CreateTexture(
        {.width = 64, .height = 48, .format = Spot::Editor::ViewportRenderer::ColorFormat, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::Editor::ViewportRenderer::ColorFormat,
                                           .sampleCount = 1,
                                           .tonemapping = {.op = Spot::TonemapOperator::Linear, .dither = false},
                                           .bloom = {.intensity = 1, .threshold = 0},
                                           .fxaa = true,
                                           .shadowMapSize = 128,
                                           .localShadowMapSize = 128});
    std::array<Spot::Scene*, 1> scenes{&scene};
    context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scenes, target.get());
    device->EndFrame();
    const auto game = device->DownloadTexture(*target);
    for (const auto* pixels : {&editor, &game}) {
        REQUIRE(pixels->size() == 64 * 48 * 4);
        int red = 0, green = 0, other = 0;
        for (std::size_t i = 0; i < pixels->size(); i += 4) {
            const int r = (*pixels)[i], g = (*pixels)[i + 1], b = (*pixels)[i + 2];
            if (r > 150 && g < 10 && b < 10)
                ++red;
            else if (g > 150 && r < 10 && b < 10)
                ++green;
            else if (r > 0 || g > 0 || b > 0)
                ++other;
        }
        CHECK(red == 4);
        CHECK(green == 4);
        CHECK(other == 0);
    }
    CHECK(sprite.GetTransform().GetPosition() == Spot::Vector3{0.1f, 0.5f, 0});
}

TEST_CASE("Tilemap-only scenes render in game and editor views offscreen", "[editor][gpu][tilemap][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    entity.GetTransform().SetPosition({-0.5f, -0.5f, -3});
    auto& map = entity.AddComponent<Spot::Tilemap>();
    map.Resize(1, 1);
    map.SetCell(0, 0, 0);
    map.SetColor(Spot::Color::Red);
    auto& main = scene.CreateEntity().AddComponent<Spot::Camera>();
    main.SetProjection(Spot::CameraProjection::Orthographic);
    main.SetOrthographicSize(1);
    Spot::Editor::ViewportRenderer viewport(*device);
    Spot::Editor::ViewportTargets targets;
    REQUIRE(targets.Resize(*device, 64, 64, viewport.GetSampleCount()));
    Spot::Editor::ViewportOptions options;
    options.showGrid = false;
    options.defaultLights = false;
    options.background = Spot::Color::Black;
    auto* context = device->BeginOffscreenFrame(*targets.GetColor());
    REQUIRE(context);
    viewport.Render(*context, targets, scene, main.GetCamera3D(), options);
    device->EndFrame();
    auto pixels = device->DownloadTexture(*targets.GetColor());
    REQUIRE(pixels.size() == 64 * 64 * 4);
    const auto center = static_cast<std::size_t>((32 * 64 + 32) * 4);
    CHECK(pixels[center] > 150);
    CHECK(pixels[center + 1] < 10);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::Editor::ViewportRenderer::ColorFormat,
                                           .sampleCount = 1,
                                           .shadowMapSize = 128,
                                           .localShadowMapSize = 128});
    std::array<Spot::Scene*, 1> scenes{&scene};
    auto gameTarget = device->CreateTexture(
        {.width = 64, .height = 64, .format = Spot::Editor::ViewportRenderer::ColorFormat, .renderTarget = true});
    REQUIRE(gameTarget);
    REQUIRE(renderer.IsValid());
    context = device->BeginOffscreenFrame(*gameTarget);
    REQUIRE(context);
    renderer.Render(*context, scenes, gameTarget.get());
    device->EndFrame();
    pixels = device->DownloadTexture(*gameTarget);
    CHECK(pixels[center] > 150);
    CHECK(pixels[center + 1] < 10);
}

namespace {

using Pixel = std::array<int, 4>;

bool IsNear(Pixel a, Pixel b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > 2) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("ImGuiRenderer draws frames clipped, and shows sRGB textures as they look", "[editor][gpu]") {
    RequireGpu();
    constexpr std::uint32_t Size = 64;

    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::Texture> gray;
    std::unique_ptr<Spot::Editor::ImGuiRenderer> renderer;
    std::vector<std::uint8_t> pixels;
    ImGuiContext* context = ImGui::CreateContext();
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& renderContext) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 2) {
            pixels = device.DownloadTexture(*target);
            return;
        }
        if (frame != 1) {
            return;
        }

        target = device.CreateTexture({.width = Size, .height = Size, .renderTarget = true});
        // Displayed as 128, which is about 0.22 once read as a linear color.
        gray = device.CreateTexture(Spot::Image(1, 1, {128, 128, 128, 255}));
        renderer = std::make_unique<Spot::Editor::ImGuiRenderer>(device, Spot::TextureFormat::Rgba8);
        REQUIRE((target && gray && renderer->IsValid()));

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {static_cast<float>(Size), static_cast<float>(Size)};
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled({0.0f, 0.0f}, {32.0f, 64.0f}, IM_COL32(255, 0, 0, 255));
        drawList->AddImage(Spot::Editor::ToImTextureId(*gray), {32.0f, 0.0f}, {64.0f, 32.0f});
        drawList->PushClipRect({32.0f, 32.0f}, {48.0f, 64.0f});
        drawList->AddRectFilled({32.0f, 32.0f}, {64.0f, 64.0f}, IM_COL32(0, 0, 255, 255));
        drawList->PopClipRect();
        ImGui::Render();

        auto pass = renderContext.BeginRenderPass({.target = target.get(), .clearColor = Spot::Color::Black});
        renderer->Render(pass, *ImGui::GetDrawData());
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    // The renderer owns ImGui's textures, so it goes before the context and after the device stopped using them.
    renderer.reset();
    ImGui::DestroyContext(context);
    REQUIRE(pixels.size() == Size * Size * 4);

    const auto at = [&](int x, int y) {
        const auto* pixel = &pixels[static_cast<std::size_t>((y * static_cast<int>(Size) + x) * 4)];
        return Pixel{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    CHECK(at(10, 10) == Pixel{255, 0, 0, 255});
    CHECK(IsNear(at(40, 10), Pixel{128, 128, 128, 255}));
    CHECK(at(40, 40) == Pixel{0, 0, 255, 255});
    CHECK(at(56, 40) == Pixel{0, 0, 0, 255});
}

TEST_CASE("Viewports pick the mesh under a pixel and outline the selection", "[editor][gpu]") {
    RequireGpu();
    constexpr std::uint32_t Size = 64;
    const Spot::Vector2 viewSize{static_cast<float>(Size), static_cast<float>(Size)};
    Spot::Camera3D camera;
    camera.position = {0.0f, 0.0f, 5.0f};

    std::unique_ptr<Spot::Editor::ViewportRenderer> renderer;
    Spot::Editor::ViewportTargets targets;
    std::unique_ptr<Spot::Mesh> cube;
    Spot::Scene scene;
    std::vector<Spot::Editor::MeshInstance> meshes;
    std::optional<std::size_t> hit;
    std::optional<std::size_t> miss;
    std::vector<std::uint8_t> pixels;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        ++frame;
        if (frame == 1) {
            renderer = std::make_unique<Spot::Editor::ViewportRenderer>(device);
            cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
            REQUIRE(cube);
            REQUIRE(targets.Resize(device, Size, Size, renderer->GetSampleCount()));
            meshes = {{cube.get(), Spot::Matrix4::Translation({-1.0f, 0.0f, 0.0f})},
                      {cube.get(), Spot::Matrix4::Translation({1.0f, 0.0f, 0.0f})}};

            // The pixel at the center of the second cube.
            const Spot::Vector4 clip = camera.GetViewProjection(viewSize) * Spot::Vector4{1.0f, 0.0f, 0.0f, 1.0f};
            const Spot::Vector2 pixel{(clip.x / clip.w * 0.5f + 0.5f) * viewSize.x,
                                      (0.5f - clip.y / clip.w * 0.5f) * viewSize.y};
            renderer->RenderPick(context, camera, viewSize, pixel, meshes);

            // A cube in the middle, outlined over an empty scene.
            renderer->Render(context, targets, scene, camera, {.showGrid = false});
            const std::array centered{Spot::Editor::MeshInstance{cube.get(), Spot::Matrix4::Identity()}};
            renderer->DrawOutline(context, targets, camera, centered, Spot::Color{1.0f, 0.0f, 0.0f, 1.0f}, 2.0f);
        } else if (frame == 2) {
            hit = renderer->ReadPick();
            renderer->RenderPick(context, camera, viewSize, {0.0f, 0.0f}, meshes);
            pixels = device.DownloadTexture(*targets.GetColor());
        } else if (frame == 3) {
            miss = renderer->ReadPick();
        }
    });
    application.Run();
    REQUIRE(application.GetRenderedFrames() >= 3);

    REQUIRE(hit.has_value());
    CHECK(*hit == 1);
    CHECK_FALSE(miss.has_value());
    // Nothing is picked twice from one drawing.
    CHECK_FALSE(renderer->ReadPick().has_value());

    REQUIRE(pixels.size() == Size * Size * 4);
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t offset = (y * Size + x) * 4;
        return Pixel{pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
    };
    // The unit cube's front face covers about 12 pixels across the middle; the line is just outside it, solid in
    // its middle and fading at its edges.
    const Pixel background = at(2, 2);
    CHECK(IsNear(at(32, 32), background));
    Pixel line = background;
    for (std::uint32_t x = 36; x <= 44; ++x) {
        if (at(x, 32)[0] > line[0]) {
            line = at(x, 32);
        }
    }
    CHECK(line[0] > 240);
    CHECK(line[1] < 80);
    CHECK(IsNear(at(52, 32), background));
}

TEST_CASE("Viewport picking and selection masks draw only the selected submesh", "[editor][gpu]") {
    RequireGpu();
    constexpr std::uint32_t Size = 64;
    std::uint32_t selected = 0;
    SECTION("First submesh") {
    }
    SECTION("Second submesh") {
        selected = 1;
    }
    SECTION("Invalid submesh") {
        selected = 42;
    }
    const bool valid = selected < 2;
    const float chosenX = selected == 0 ? -1.0f : 1.0f;
    const Spot::Vector2 viewSize{static_cast<float>(Size), static_cast<float>(Size)};
    Spot::Camera3D camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    const auto pixelAt = [&](float x) {
        const Spot::Vector4 clip = camera.GetViewProjection(viewSize) * Spot::Vector4{x, 0.0f, 0.0f, 1.0f};
        return Spot::Vector2{(clip.x / clip.w * 0.5f + 0.5f) * viewSize.x, viewSize.y * 0.5f};
    };
    std::unique_ptr<Spot::Editor::ViewportRenderer> renderer;
    Spot::Editor::ViewportTargets targets;
    std::unique_ptr<Spot::Mesh> mesh;
    std::vector<Spot::Editor::MeshInstance> instances;
    std::optional<std::size_t> hit;
    std::optional<std::size_t> miss;
    std::vector<std::uint8_t> mask;
    int frame = 0;
    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (++frame == 1) {
            Spot::MeshData combined;
            for (const float x : {-1.0f, 1.0f}) {
                Spot::MeshData cube = Spot::MeshData::CreateCube();
                for (auto& vertex : cube.vertices) {
                    vertex.position.x += x;
                }
                combined.AppendSubmesh(cube);
            }
            mesh = Spot::Mesh::Create(device, combined);
            REQUIRE(mesh);
            renderer = std::make_unique<Spot::Editor::ViewportRenderer>(device);
            REQUIRE(targets.Resize(device, Size, Size, renderer->GetSampleCount()));
            instances = {{mesh.get(), Spot::Matrix4::Identity(), selected}};
            renderer->RenderPick(context, camera, viewSize, pixelAt(chosenX), instances);
            Spot::Scene empty;
            renderer->Render(context, targets, empty, camera, {.showGrid = false});
            renderer->DrawOutline(context, targets, camera, instances, Spot::Color::Red, 2.0f);
        } else if (frame == 2) {
            hit = renderer->ReadPick();
            mask = device.DownloadTexture(*targets.GetSelectionMask());
            renderer->RenderPick(context, camera, viewSize, pixelAt(-chosenX), instances);
        } else if (frame == 3) {
            miss = renderer->ReadPick();
        }
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(application.GetRenderedFrames() >= 3);
    CHECK(hit.has_value() == valid);
    if (hit) {
        CHECK(*hit == 0);
    }
    CHECK_FALSE(miss);
    REQUIRE(mask.size() == Size * Size * 4);
    const auto at = [&](float x) {
        const Spot::Vector2 pixel = pixelAt(x);
        return mask[(static_cast<std::uint32_t>(pixel.y) * Size + static_cast<std::uint32_t>(pixel.x)) * 4];
    };
    CHECK(at(chosenX) == (valid ? 255 : 0));
    CHECK(at(-chosenX) == 0);
}

TEST_CASE("Thumbnails show materials on a sphere over a clear background", "[editor][gpu]") {
    RequireGpu();
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Game", Spot::ProjectTemplate::Empty);
    REQUIRE(file.has_value());
    const Spot::MaterialSource red{.baseColor = Spot::Color{1.0f, 0.0f, 0.0f, 1.0f}};
    REQUIRE(red.SaveToFile(file->parent_path() / "assets" / "Red.material"));

    std::unique_ptr<Spot::Editor::EditorProject> project;
    std::unique_ptr<Spot::Editor::ThumbnailCache> thumbnails;
    std::optional<Spot::AssetId> material;
    const Spot::Texture* picture = nullptr;
    std::vector<std::uint8_t> pixels;
    int frame = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        ++frame;
        if (frame == 1) {
            project = Spot::Editor::EditorProject::Open(*file, &device);
            REQUIRE(project);
            material = project->GetAssetDatabase().FindByPath("Red.material");
            REQUIRE(material.has_value());
            thumbnails = std::make_unique<Spot::Editor::ThumbnailCache>(device);
            // Nothing until it is drawn.
            CHECK(thumbnails->Get(*project, *material, Spot::AssetType::Material) == nullptr);
            CHECK(thumbnails->Get(*project, *material, Spot::AssetType::Scene) == nullptr);
            thumbnails->Render(context, *project);
        } else if (frame == 2) {
            picture = thumbnails->Get(*project, *material, Spot::AssetType::Material);
            REQUIRE(picture);
            pixels = device.DownloadTexture(*picture);
        }
    });
    application.Run();
    REQUIRE(application.GetRenderedFrames() >= 2);

    constexpr std::uint32_t Size = Spot::Editor::ThumbnailCache::Size;
    REQUIRE(pixels.size() == Size * Size * 4);
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t offset = (y * Size + x) * 4;
        return Pixel{pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
    };
    const Pixel center = at(Size / 2, Size / 2);
    CHECK(center[0] > 100);
    CHECK(center[1] < center[0] / 2);
    CHECK(center[3] == 255);
    CHECK(at(1, 1)[3] == 0);
    thumbnails->Clear();
}

TEST_CASE("Models dropped into scenes preserve their nodes and share mesh buffers", "[editor][gpu]") {
    RequireGpu();
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Game", Spot::ProjectTemplate::Empty);
    REQUIRE(file.has_value());
    const auto assets = file->parent_path() / "assets";
    std::filesystem::copy_file(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models" / "cube.glb",
                               assets / "cube.glb");

    std::unique_ptr<Spot::Editor::EditorProject> project;
    Spot::Editor::SceneDocument document;
    bool created = false;
    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        if (!project) {
            project = Spot::Editor::EditorProject::Open(*file, &app.GetGraphicsDevice());
            REQUIRE(project);
            // Scenes load assets through the running application's manager, which the editor makes the project's.
            Spot::Detail::SetAssetManager(&project->GetAssets());
            created = Spot::Editor::CreateEntityFromAsset(document, *project,
                                                          *project->GetAssetDatabase().FindByPath("cube.glb"));
            Spot::Detail::SetAssetManager(&app.GetAssets());
        }
    });
    application.Run();

    REQUIRE(created);
    const Spot::Entity* root = document.GetScene().GetEntity(document.GetSelection().GetPrimary());
    REQUIRE(root);
    CHECK(root->GetName() == "cube");
    REQUIRE(root->GetChildren().size() == 1);
    const Spot::Entity* pivot = root->FindChild("Pivot");
    REQUIRE(pivot);
    const Spot::Entity* cube = pivot->FindChild("Cube");
    REQUIRE(cube);
    CHECK(Spot::Distance(cube->GetTransform().GetWorldPosition(), {11.0f, 0.0f, 0.0f}) < 1e-4f);
    const auto renderers = cube->GetComponents<Spot::MeshRenderer>();
    REQUIRE(renderers.size() == 2);
    CHECK(renderers[0]->GetMesh().Get() == renderers[1]->GetMesh().Get());
    for (std::size_t i = 0; i < renderers.size(); ++i) {
        const auto* renderer = renderers[i];
        CHECK(renderer->GetSubmesh() == i);
        CHECK(project->GetAssetDatabase().Find(renderer->GetMesh().GetId())->type == Spot::AssetType::Mesh);
        CHECK(renderer->GetMaterial().GetId().IsValid());
    }
}

TEST_CASE("Selected particle previews render through the editor viewport offscreen",
          "[editor][gpu][particles][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Editor::ViewportRenderer renderer(*device);
    Spot::Editor::ViewportTargets targets;
    REQUIRE(targets.Resize(*device, 64, 64, renderer.GetSampleCount()));
    Spot::Editor::SceneDocument document;
    auto& entity = document.GetScene().CreateEntity();
    entity.GetTransform().SetPosition({0, 0, -3});
    auto& effect = entity.AddComponent<Spot::ParticleSystem>();
    Spot::ParticleSettings settings;
    settings.emissionRate = 0;
    settings.bursts = {{0, 4}};
    settings.size = {1, 1};
    settings.speed = {0, 0};
    settings.gravity = {};
    settings.startColor = Spot::Color::Red;
    effect.SetSettings(settings);
    auto& preview = document.GetParticlePreview();
    preview.Select(document.GetScene(), entity.GetId());
    preview.Update(document.GetScene(), 0.1f);
    auto* context = device->BeginOffscreenFrame(*targets.GetColor());
    REQUIRE(context);
    Spot::Camera3D camera;
    camera.orthographic = true;
    camera.orthographicSize = 1;
    Spot::Editor::ViewportOptions options;
    options.showGrid = false;
    options.defaultLights = false;
    options.background = Spot::Color::Black;
    renderer.Render(*context, targets, document.GetScene(), camera, options);
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*targets.GetColor());
    REQUIRE(pixels.size() == 64 * 64 * 4);
    const auto center = (32 * 64 + 32) * 4;
    CHECK(pixels[center] > 150);
    CHECK(pixels[center + 1] < 10);
    CHECK(pixels[center + 2] < 10);
    CHECK(pixels[0] == 0);
    CHECK(document.GetScene().GetUpdateCount() == 0);
    CHECK(document.GetHistory().GetUndoCount() == 0);
}

TEST_CASE("Authored canvas widgets preview through the editor viewport offscreen", "[editor][gpu][ui][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Editor::ViewportRenderer renderer(*device);
    Spot::Editor::ViewportTargets targets;
    REQUIRE(targets.Resize(*device, 64, 64, renderer.GetSampleCount()));
    Spot::Editor::SceneDocument document;
    auto& root = document.GetScene().CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& entity = root.CreateChild("Image");
    entity.AddComponent<Spot::UiRect>().layout.sizeDelta = {32, 32};
    entity.AddComponent<Spot::UiImage>().tint = Spot::Color::Green;
    auto* context = device->BeginOffscreenFrame(*targets.GetColor());
    REQUIRE(context);
    Spot::Editor::ViewportOptions options;
    options.showGrid = false;
    options.defaultLights = false;
    options.background = Spot::Color::Black;
    options.drawUi = true;
    renderer.Render(*context, targets, document.GetScene(), {}, options);
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*targets.GetColor());
    CHECK(pixels[(32 * 64 + 32) * 4 + 1] > 240);
    CHECK(pixels[(32 * 64 + 32) * 4] == 0);
    CHECK(pixels[0] == 0);
    CHECK(document.GetScene().GetUpdateCount() == 0);
    CHECK(document.GetHistory().GetUndoCount() == 0);
}

TEST_CASE("Skinned edit-mode previews, picking and outlines follow the animated pose",
          "[editor][gpu][animation][offscreen]") {
    RequireGpu();
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Editor::ViewportRenderer renderer(*device);
    Spot::Editor::ViewportTargets targets;
    REQUIRE(targets.Resize(*device, 96, 96, renderer.GetSampleCount()));
    auto data = Spot::MeshData::CreateCube(0.5f);
    data.skin.resize(data.vertices.size());
    const Spot::AssetRef<Spot::Mesh> mesh = Spot::Mesh::Create(*device, data);
    REQUIRE(mesh);
    Spot::Editor::SceneDocument document;
    auto& entity = document.GetScene().CreateEntity("Animated");
    entity.GetTransform().SetPosition({0, 0, -3});
    auto& animator = entity.AddComponent<Spot::Animator>();
    animator.SetSkeleton(
        std::make_shared<const Spot::Skeleton>(Spot::Skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Spot::Matrix4{}}}));
    REQUIRE(animator.Play(std::make_shared<const Spot::AnimationClip>(Spot::AnimationClip{
        "Move",
        1,
        {{0, Spot::AnimationPath::Translation, Spot::AnimationInterpolation::Linear, {0, 1}, {{}, {2, 0, 0, 0}}}}})));
    auto& skin = entity.AddComponent<Spot::SkinnedMeshRenderer>();
    skin.SetMesh(mesh);
    skin.SetAnimator(entity.GetId());
    skin.SetMaterial(
        std::make_shared<const Spot::Material>(Spot::Material{.baseColor = Spot::Color::Red, .metallic = 0}));
    auto& preview = document.GetAnimationPreview();
    preview.Select(document.GetScene(), entity.GetId());
    preview.Seek(document.GetScene(), 0.5f);
    Spot::Camera3D camera;
    camera.orthographic = true;
    camera.orthographicSize = 2;
    Spot::Editor::ViewportOptions options;
    options.showGrid = false;
    options.background = Spot::Color::Black;
    auto* context = device->BeginOffscreenFrame(*targets.GetColor());
    REQUIRE(context);
    renderer.Render(*context, targets, document.GetScene(), camera, options);
    const std::array meshes{Spot::Editor::MeshInstance{mesh.Get(), entity.GetTransform().GetWorldMatrix(),
                                                       Spot::Mesh::AllSubmeshes, skin.GetPalette()}};
    renderer.DrawOutline(*context, targets, camera, meshes, Spot::Color::Green, 2);
    renderer.RenderPick(*context, camera, {96, 96}, {72, 48}, meshes);
    device->EndFrame();
    CHECK(renderer.ReadPick() == 0);
    const auto pixels = device->DownloadTexture(*targets.GetColor());
    CHECK(pixels[(48 * 96 + 72) * 4] > 100);
    CHECK(pixels[(48 * 96 + 48) * 4] == 0);
    std::size_t green = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        green += pixels[i + 1] > pixels[i] + 20 ? 1 : 0;
    }
    CHECK(green > 10);
    context = device->BeginOffscreenFrame(*targets.GetColor());
    REQUIRE(context);
    renderer.RenderPick(*context, camera, {96, 96}, {48, 48}, meshes);
    device->EndFrame();
    CHECK_FALSE(renderer.ReadPick());
    CHECK(document.GetScene().GetUpdateCount() == 0);
    CHECK(document.GetHistory().GetUndoCount() == 0);
}
