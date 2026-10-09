#include "headless_imgui.h"
#include "temp_directory.h"

#include "app/editor_context.h"
#include "app/shortcuts_window.h"
#include "core/editor_settings.h"
#include "panels/project_settings_panel.h"
#include "project/editor_project.h"
#include "project/new_project_dialog.h"
#include "project/project_hub.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("The new project dialog creates a project from what is typed into it", "[editor][project][ui]") {
    const TempDirectory temp;
    const auto window = Spot::Window::Create();
    REQUIRE(window != nullptr);
    Spot::Tests::HeadlessImGui imgui;
    Spot::Editor::EditorSettings settings;
    settings.SetProjectsLocation(temp.GetPath());
    Spot::Editor::NewProjectDialog dialog(settings, *window);

    dialog.Open();
    std::optional<std::filesystem::path> created;
    const auto frame = [&] {
        imgui.BeginFrame();
        if (auto result = dialog.Draw()) {
            created = std::move(result);
        }
        imgui.EndFrame();
    };
    frame();
    frame();
    // The name field has focus with its text selected, so typing replaces it.
    ImGui::GetIO().AddInputCharactersUTF8("Typed Game");
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    frame();

    REQUIRE(created.has_value());
    CHECK(*created == std::filesystem::absolute(temp.GetPath() / "Typed Game" / "Typed Game.spot"));
    CHECK(std::filesystem::is_regular_file(*created));
}

TEST_CASE("The hub lists recent projects, missing ones included", "[editor][project][ui]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Listed", Spot::ProjectTemplate::Empty);
    REQUIRE(file.has_value());
    Spot::Tests::HeadlessImGui imgui;
    Spot::Editor::EditorSettings settings;
    settings.AddRecentProject(temp.GetPath() / "Gone" / "Gone.spot", "Gone");
    settings.AddRecentProject(*file, "Listed");
    int opened = 0;
    Spot::Editor::ProjectHub hub(settings, {.openProject = [&](const std::filesystem::path&) { ++opened; },
                                            .browseForProject = [] {},
                                            .newProject = [] {}});

    for (int i = 0; i < 3; ++i) {
        imgui.BeginFrame();
        hub.Draw();
        imgui.EndFrame();
    }

    CHECK(opened == 0);
    CHECK(settings.GetRecentProjects().size() == 2);
}

TEST_CASE("The project settings panel shows the open project", "[editor][project][ui]") {
    const TempDirectory temp;
    const auto window = Spot::Window::Create();
    REQUIRE(window != nullptr);
    const auto file = Spot::CreateProject(temp.GetPath(), "Shown", Spot::ProjectTemplate::Basic3D);
    REQUIRE(file.has_value());
    Spot::Tests::HeadlessImGui imgui;
    Spot::Editor::EditorSettings settings;

    Spot::Editor::EditorContext context(*window, nullptr, settings);
    context.SetProject(Spot::Editor::EditorProject::Open(*file, nullptr));
    Spot::Editor::ProjectSettingsPanel panel(context);
    panel.SetOpen(true);

    for (int i = 0; i < 3; ++i) {
        imgui.BeginFrame();
        panel.Draw();
        imgui.EndFrame();
    }

    CHECK(panel.IsOpen());
}

TEST_CASE("The shortcuts list names every shortcut once per place, and finds them by keys or action", "[editor][ui]") {
    Spot::Tests::HeadlessImGui imgui;
    imgui.BeginFrame();
    std::size_t count = 0;
    for (const Spot::Editor::ShortcutGroup& group : Spot::Editor::GetShortcutGroups()) {
        std::vector<std::string> inputs;
        for (const Spot::Editor::ShortcutInfo& shortcut : group.shortcuts) {
            const std::string input = Spot::Editor::GetShortcutInput(shortcut);
            CHECK_FALSE(input.empty());
            CHECK(std::string_view(shortcut.action).size() > 0);
            // One key does one thing in a place.
            CHECK(std::ranges::find(inputs, input) == inputs.end());
            inputs.push_back(input);
            ++count;
        }
    }
    CHECK(count > 30);
    CHECK(Spot::Editor::GetShortcutInput({ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, nullptr, "Save"}) ==
          "Ctrl+Shift+S");
    CHECK(Spot::Editor::GetShortcutInput({ImGuiMod_Ctrl | ImGuiKey_Comma, nullptr, "Preferences"}) == "Ctrl+,");
    imgui.EndFrame();
}
