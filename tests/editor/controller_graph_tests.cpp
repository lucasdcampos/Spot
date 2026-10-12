#include "headless_imgui.h"
#include "inspector/asset_inspector.h"
#include "inspector/controller_graph.h"
#include "temp_directory.h"

#include <catch2/catch_test_macros.hpp>
#include <spot.h>
#include <spot_tools.h>

#include <fstream>

TEST_CASE("Removing a controller graph state keeps transition and entry identities consistent", "[editor][animation]") {
    const auto clip = std::make_shared<const Spot::AnimationClip>(Spot::AnimationClip{"Clip", 1, {}});
    Spot::AnimatorController controller;
    controller.states = {{"A", clip}, {"B", clip}, {"C", clip}};
    controller.entryState = 2;
    controller.transitions = {{0, 1}, {1, 2}, {0, 2}, {-1, 2}};
    REQUIRE(Spot::Editor::ControllerGraph::RemoveState(controller, 1));
    REQUIRE(controller.IsValid());
    CHECK(controller.entryState == 1);
    REQUIRE(controller.transitions.size() == 2);
    CHECK(controller.transitions[0].from == 0);
    CHECK(controller.transitions[0].to == 1);
    CHECK(controller.transitions[1].from == -1);
    CHECK_FALSE(Spot::Editor::ControllerGraph::RemoveState(controller, 9));
    REQUIRE(Spot::Editor::ControllerGraph::RemoveState(controller, 1));
    CHECK_FALSE(Spot::Editor::ControllerGraph::RemoveState(controller, 0));
}

TEST_CASE("Controller graph edits apply to source files and undo as one change", "[editor][animation][inspector]") {
    TempDirectory directory;
    const auto file = Spot::CreateProject(directory.GetPath(), "Graph", Spot::ProjectTemplate::Empty);
    REQUIRE(file);
    auto project = Spot::Editor::EditorProject::Open(*file, nullptr);
    REQUIRE(project);
    const auto assets = project->GetProject().GetAssetsDirectory();
    std::filesystem::copy_file(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models/animation_only.gltf",
                               assets / "rig.gltf");
    project->Refresh();
    Spot::AnimatorController controller;
    controller.states = {{"Idle", project->GetAssets().Load<Spot::AnimationClip>("rig.gltf#Move")}};
    std::ofstream(assets / "robot.controller") << controller.SaveToString();
    project->Refresh();
    const auto id = project->GetAssets().FindByPath("robot.controller");
    REQUIRE(id);
    const auto window = Spot::Window::Create();
    REQUIRE(window);
    Spot::Tests::HeadlessImGui imgui({1024, 1200});
    Spot::Editor::EditorSettings settings;
    Spot::Editor::EditorContext context(*window, nullptr, settings);
    context.SetProject(std::move(project));
    Spot::Editor::AssetInspector inspector(context);
    imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1000, 1180});
        ImGui::Begin("Controller");
        inspector.Draw(*context.GetProject(), *id);
        ImGui::End();
    });
    imgui.Frame();
    imgui.Frame();
    const auto click = [&](std::string_view item) {
        const auto rect = inspector.GetItemRect(item);
        REQUIRE(rect);
        imgui.Click({(rect->first.x + rect->second.x) * 0.5f, (rect->first.y + rect->second.y) * 0.5f});
    };
    click("Controller/AddState");
    CHECK(context.GetProject()->GetAssets().Load<Spot::AnimatorController>(*id)->states.size() == 1);
    click("Controller/Apply");
    CHECK(context.GetProject()->GetAssets().Load<Spot::AnimatorController>(*id)->states.size() == 2);
    auto* history = inspector.GetHistory();
    REQUIRE(history);
    CHECK(history->GetUndoCount() == 1);
    history->Undo();
    CHECK(context.GetProject()->GetAssets().Load<Spot::AnimatorController>(*id)->states.size() == 1);
    history->Redo();
    CHECK(context.GetProject()->GetAssets().Load<Spot::AnimatorController>(*id)->states.size() == 2);
}
