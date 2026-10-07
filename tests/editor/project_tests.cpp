#include "temp_directory.h"

#include "core/editor_settings.h"
#include "imgui/widgets.h"
#include "project/editor_project.h"
#include "project/project_templates.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace std::chrono_literals;

TEST_CASE("Recent projects keep the latest first, once each, up to a limit", "[editor][settings]") {
    Spot::Editor::EditorSettings settings;
    settings.AddRecentProject("a/A.spot", "A");
    settings.AddRecentProject("b/B.spot", "B");
    settings.AddRecentProject("a/./A.spot", "A again");

    REQUIRE(settings.GetRecentProjects().size() == 2);
    CHECK(settings.GetRecentProjects()[0].name == "A again");
    CHECK(settings.GetRecentProjects()[1].name == "B");

    for (int i = 0; i < 20; ++i) {
        settings.AddRecentProject(std::filesystem::path("p") / std::to_string(i), std::to_string(i));
    }
    CHECK(settings.GetRecentProjects().size() == Spot::Editor::EditorSettings::MaxRecentProjects);
    CHECK(settings.GetRecentProjects().front().name == "19");

    settings.RemoveRecentProject(std::filesystem::path("p") / "19");
    CHECK(settings.GetRecentProjects().front().name == "18");
}

TEST_CASE("Editor settings survive a restart", "[editor][settings]") {
    const TempDirectory temp;
    const auto file = temp.GetPath() / "settings" / "settings.json";
    const auto opened = std::chrono::system_clock::time_point(std::chrono::seconds(1700000000));
    {
        Spot::Editor::EditorSettings settings(file);
        settings.AddRecentProject(temp.GetPath() / u8"Café" / u8"Café.spot", "Café", opened);
        settings.SetProjectsLocation(temp.GetPath() / "Projects");
        settings.GetGizmo().tool = Spot::Editor::GizmoTool::Rotate;
        settings.GetGizmo().local = true;
        settings.GetGizmo().rotateStep = 5.0f;
        REQUIRE(settings.Save());
    }

    Spot::Editor::EditorSettings loaded(file);
    loaded.Load();

    REQUIRE(loaded.GetRecentProjects().size() == 1);
    CHECK(loaded.GetRecentProjects()[0].file == temp.GetPath() / u8"Café" / u8"Café.spot");
    CHECK(loaded.GetRecentProjects()[0].name == "Café");
    CHECK(loaded.GetRecentProjects()[0].lastOpened == opened);
    CHECK(loaded.GetProjectsLocation() == temp.GetPath() / "Projects");
    CHECK(loaded.GetGizmo().tool == Spot::Editor::GizmoTool::Rotate);
    CHECK(loaded.GetGizmo().local);
    CHECK_FALSE(loaded.GetGizmo().snap);
    CHECK(loaded.GetGizmo().rotateStep == 5.0f);
}

TEST_CASE("Unreadable editor settings leave the defaults", "[editor][settings]") {
    const TempDirectory temp;
    const auto file = temp.GetPath() / "settings.json";
    std::ofstream(file) << "{ not json";

    Spot::Editor::EditorSettings settings(file);
    settings.Load();

    CHECK(settings.GetRecentProjects().empty());
    CHECK(settings.GetProjectsLocation() == Spot::Editor::GetDefaultProjectsLocation());
}

TEST_CASE("Project names must work as folder and file names", "[editor][project]") {
    CHECK(Spot::Editor::ValidateProjectName("My Game").empty());
    CHECK(Spot::Editor::ValidateProjectName("Jogo de Ação 2").empty());
    CHECK_FALSE(Spot::Editor::ValidateProjectName("").empty());
    CHECK_FALSE(Spot::Editor::ValidateProjectName("a/b").empty());
    CHECK_FALSE(Spot::Editor::ValidateProjectName("what?").empty());
    CHECK_FALSE(Spot::Editor::ValidateProjectName(" padded").empty());
    CHECK_FALSE(Spot::Editor::ValidateProjectName("dotted.").empty());
}

TEST_CASE("The 3D template makes a project that opens on a lit scene", "[editor][project]") {
    const TempDirectory temp;

    const auto file = Spot::Editor::CreateProject(temp.GetPath(), "Demo", Spot::Editor::ProjectTemplate::Basic3D);

    REQUIRE(file.has_value());
    const auto directory = temp.GetPath() / "Demo";
    CHECK(*file == std::filesystem::absolute(directory / "Demo.spot"));
    CHECK(std::filesystem::is_regular_file(directory / ".gitignore"));
    CHECK(std::filesystem::is_regular_file(directory / "assets" / "scenes" / "Main.scene"));
    CHECK(std::filesystem::is_regular_file(directory / "assets" / "scenes" / "Main.scene.meta"));

    const auto project = Spot::Editor::EditorProject::Open(*file, nullptr);
    REQUIRE(project != nullptr);
    const Spot::AssetId startup = project->GetProject().GetSettings().startupScene;
    REQUIRE(startup.IsValid());
    const Spot::AssetInfo* scene = project->GetAssetDatabase().Find(startup);
    REQUIRE(scene != nullptr);
    CHECK(scene->path == "scenes/Main.scene");

    const auto instance = project->GetAssets().InstantiateScene(startup);
    REQUIRE(instance != nullptr);
    CHECK(instance->FindComponent<Spot::DirectionalLight>() != nullptr);
    CHECK(instance->FindComponent<Spot::Sky>() != nullptr);
    const Spot::Entity* cube = instance->FindEntity("Cube");
    REQUIRE(cube != nullptr);
    const auto* renderer = cube->GetComponent<Spot::MeshRenderer>();
    REQUIRE(renderer != nullptr);
    CHECK(renderer->GetMesh().GetId() == Spot::BuiltInAssets::CubeMesh);
}

TEST_CASE("Empty projects only have their file and assets folder", "[editor][project]") {
    const TempDirectory temp;

    const auto file = Spot::Editor::CreateProject(temp.GetPath(), "Blank", Spot::Editor::ProjectTemplate::Empty);

    REQUIRE(file.has_value());
    CHECK(std::filesystem::is_empty(temp.GetPath() / "Blank" / "assets"));
    const auto project = Spot::Editor::EditorProject::Open(*file, nullptr);
    REQUIRE(project != nullptr);
    CHECK(project->GetAssetDatabase().GetAssets().empty());
    CHECK_FALSE(project->GetProject().GetSettings().startupScene.IsValid());
}

TEST_CASE("Projects are never created over a folder that has files", "[editor][project]") {
    const TempDirectory temp;
    std::filesystem::create_directories(temp.GetPath() / "Taken");
    std::ofstream(temp.GetPath() / "Taken" / "notes.txt") << "mine";

    CHECK_FALSE(Spot::Editor::CreateProject(temp.GetPath(), "Taken", Spot::Editor::ProjectTemplate::Empty));
    CHECK_FALSE(Spot::Editor::CreateProject(temp.GetPath(), "bad/name", Spot::Editor::ProjectTemplate::Empty));
}

TEST_CASE("Times read as how long ago they were", "[editor][widgets]") {
    const auto now = std::chrono::system_clock::time_point(std::chrono::sys_days(std::chrono::year(2026) / 10 / 5));

    CHECK(Spot::Editor::Widgets::FormatRelativeTime(now - 10s, now) == "just now");
    CHECK(Spot::Editor::Widgets::FormatRelativeTime(now - 1min, now) == "a minute ago");
    CHECK(Spot::Editor::Widgets::FormatRelativeTime(now - 5min, now) == "5 minutes ago");
    CHECK(Spot::Editor::Widgets::FormatRelativeTime(now - 3h, now) == "3 hours ago");
    CHECK(Spot::Editor::Widgets::FormatRelativeTime(now - 30h, now) == "yesterday");
    CHECK(Spot::Editor::Widgets::FormatRelativeTime(now - std::chrono::days(4), now) == "4 days ago");
    CHECK(Spot::Editor::Widgets::FormatRelativeTime(now - std::chrono::days(60), now) == "2026-08-06");
}
