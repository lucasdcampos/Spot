#include "rendering_application.h"

#include "imgui/imgui_renderer.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
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
