#include "core/console_log.h"
#include "editor/headless_imgui.h"
#include "panels/console_panel.h"

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

TEST_CASE("Editor console command lines execute the shared registry and record failures", "[editor][console]") {
    Spot::Tests::HeadlessImGui ui;
    auto log = std::make_shared<Spot::Editor::ConsoleLog>();
    Spot::Log::AddSink(log);
    struct SinkScope {
        std::shared_ptr<Spot::LogSink> sink;
        ~SinkScope() { Spot::Log::RemoveSink(sink); }
    } scope{log};
    Spot::CVar<int> value(Spot::Console::Get(), "test.editor.value", 3);
    Spot::Editor::ConsolePanel panel(log);
    panel.ExecuteCommand("test.editor.value 7");
    CHECK(value.Get() == 7);
    CHECK(panel.GetCommandLine().empty());
    CHECK_FALSE(panel.GetRows().empty());
    panel.ExecuteCommand("test.editor.unknown");
    CHECK_FALSE(Spot::Console::Get().GetOutput().back().success);
    ui.SetDraw([&] { panel.Draw(); });
    ui.Frame();
    CHECK_FALSE(panel.GetRows().empty());
}

TEST_CASE("Editor console fields accept typing, Tab completion, Enter and history navigation", "[editor][console]") {
    Spot::Tests::HeadlessImGui ui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    auto log = std::make_shared<Spot::Editor::ConsoleLog>();
    Spot::CVar<int> value(Spot::Console::Get(), "test.editor.input", 0);
    Spot::Editor::ConsolePanel panel(log);
    ui.SetDraw([&] {
        ImGui::SetNextWindowSize({800.0f, 400.0f}, ImGuiCond_Always);
        panel.Draw();
    });
    ui.Frame();
    auto* window = ImGui::FindWindowByName(panel.GetWindowName().c_str());
    REQUIRE(window);
    ImGui::ActivateItemByID(window->GetID("##Command"));
    ui.Frame();
    ImGui::GetIO().AddInputCharactersUTF8("test.editor.in");
    ui.Frame();
    CHECK(panel.GetCommandLine() == "test.editor.in");
    const auto press = [&](ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        ui.Frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        ui.Frame();
    };
    press(ImGuiKey_Tab);
    CHECK(panel.GetCommandLine() == "test.editor.input ");
    ImGui::GetIO().AddInputCharactersUTF8("11");
    ui.Frame();
    press(ImGuiKey_Enter);
    CHECK(value.Get() == 11);
    CHECK(panel.GetCommandLine().empty());
    press(ImGuiKey_UpArrow);
    CHECK(panel.GetCommandLine() == "test.editor.input 11");
    press(ImGuiKey_DownArrow);
    CHECK(panel.GetCommandLine().empty());
}
