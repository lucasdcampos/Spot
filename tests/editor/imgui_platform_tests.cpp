#include "headless_imgui.h"

#include "imgui/imgui_platform.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace {

struct PlatformFixture {
    std::unique_ptr<Spot::Window> window = Spot::Window::Create({.width = 640, .height = 480});
    Spot::Tests::HeadlessImGui imgui;
    std::unique_ptr<Spot::Editor::ImGuiPlatform> platform;

    PlatformFixture() {
        REQUIRE(window != nullptr);
        platform = std::make_unique<Spot::Editor::ImGuiPlatform>(*window);
    }

    void Frame() {
        platform->NewFrame(1.0f / 60.0f);
        imgui.BeginFrame();
        imgui.EndFrame();
    }
};

} // namespace

TEST_CASE("ImGuiPlatform sizes the display after the window", "[editor][imgui]") {
    PlatformFixture fixture;

    fixture.Frame();

    const ImGuiIO& io = ImGui::GetIO();
    CHECK(io.DisplaySize.x == 640.0f);
    CHECK(io.DisplaySize.y == 480.0f);
    CHECK(io.DisplayFramebufferScale.x >= 1.0f);
    CHECK(io.DeltaTime > 0.0f);
}

TEST_CASE("ImGuiPlatform merges left and right modifier keys into ImGui modifiers", "[editor][imgui]") {
    PlatformFixture fixture;

    fixture.platform->ProcessEvent(Spot::KeyPressedEvent{Spot::Key::RightControl, false});
    fixture.platform->ProcessEvent(Spot::KeyPressedEvent{Spot::Key::LeftShift, false});
    fixture.platform->ProcessEvent(Spot::KeyPressedEvent{Spot::Key::S, false});
    fixture.Frame();

    CHECK(ImGui::GetIO().KeyCtrl);
    CHECK(ImGui::GetIO().KeyShift);
    CHECK(ImGui::IsKeyDown(ImGuiKey_S));

    fixture.platform->ProcessEvent(Spot::KeyReleasedEvent{Spot::Key::RightControl});
    fixture.platform->ProcessEvent(Spot::WindowFocusEvent{false});
    fixture.Frame();

    CHECK_FALSE(ImGui::GetIO().KeyCtrl);
    // Keys released elsewhere never arrive, so losing focus lets go of the modifiers too.
    CHECK_FALSE(ImGui::GetIO().KeyShift);
}

TEST_CASE("ImGuiPlatform types text and moves the mouse", "[editor][imgui]") {
    PlatformFixture fixture;

    fixture.platform->ProcessEvent(Spot::TextInputEvent{"\xC3\xA9"});
    fixture.platform->ProcessEvent(Spot::MouseMovedEvent{{100.0f, 50.0f}, {1.0f, 1.0f}});
    fixture.platform->ProcessEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    fixture.platform->NewFrame(1.0f / 60.0f);
    fixture.imgui.BeginFrame();

    const ImGuiIO& io = ImGui::GetIO();
    REQUIRE(io.InputQueueCharacters.Size == 1);
    CHECK(io.InputQueueCharacters[0] == 0xE9);
    CHECK(io.MousePos.x == 100.0f);
    CHECK(io.MousePos.y == 50.0f);
    CHECK(ImGui::IsMouseDown(ImGuiMouseButton_Right));
    fixture.imgui.EndFrame();

    fixture.platform->ProcessEvent(Spot::MouseLeaveEvent{});
    fixture.Frame();
    CHECK_FALSE(ImGui::IsMousePosValid());
}

TEST_CASE("ImGuiPlatform shares the OS clipboard with ImGui", "[editor][imgui]") {
    PlatformFixture fixture;

    ImGui::SetClipboardText("from ImGui");
    CHECK(Spot::Clipboard::GetText() == "from ImGui");

    REQUIRE(Spot::Clipboard::SetText("from the OS"));
    CHECK(std::string(ImGui::GetClipboardText()) == "from the OS");
}
