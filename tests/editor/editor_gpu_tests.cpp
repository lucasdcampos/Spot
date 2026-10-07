#include "rendering_application.h"

#include "imgui/imgui_renderer.h"
#include "viewport/viewport_renderer.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

using Spot::Tests::RenderingApplication;
using Spot::Tests::RequireGpu;

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
