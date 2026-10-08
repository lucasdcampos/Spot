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
        settings.GetPreferences() = {
            .uiScale = 1.25f, .clearConsoleOnPlay = true, .buildOnChange = false, .backupMinutes = 10};
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
    CHECK(loaded.GetPreferences().uiScale == 1.25f);
    CHECK(loaded.GetPreferences().clearConsoleOnPlay);
    CHECK_FALSE(loaded.GetPreferences().buildOnChange);
    CHECK(loaded.GetPreferences().backupMinutes == 10);
}

TEST_CASE("Preferences out of range come back within it", "[editor][settings]") {
    const TempDirectory temp;
    const auto file = temp.GetPath() / "settings.json";
    std::ofstream(file) << R"({"preferences":{"uiScale":40.0,"backupMinutes":-3}})";

    Spot::Editor::EditorSettings settings(file);
    settings.Load();

    CHECK(settings.GetPreferences().uiScale == Spot::Editor::EditorPreferences::MaxUiScale);
    CHECK(settings.GetPreferences().backupMinutes == 0);
    CHECK(settings.GetPreferences().buildOnChange);
}

TEST_CASE("Without building on change, changes to the code are only told of", "[editor][project]") {
    const TempDirectory temp;
    const auto file = Spot::Editor::CreateProject(temp.GetPath(), "Coded", Spot::Editor::ProjectTemplate::Code3D);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    Spot::Editor::ModuleHost host(*project, {.watchInterval = std::chrono::milliseconds(0)});
    host.SetBuildOnChange(false);
    host.Update();
    CHECK_FALSE(host.HasUnbuiltChanges());

    std::ofstream(project->GetDirectory() / "src" / "more.cpp") << "// More of the game\n";
    host.Update();

    CHECK(host.HasUnbuiltChanges());
    CHECK_FALSE(host.GetBuild().IsRunning());
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

TEST_CASE("The 3D with C++ template adds the game's code, whose component the scene keeps until it is built",
          "[editor][project]") {
    const TempDirectory temp;

    const auto file = Spot::Editor::CreateProject(temp.GetPath(), "My Game", Spot::Editor::ProjectTemplate::Code3D);

    REQUIRE(file.has_value());
    const auto directory = temp.GetPath() / "My Game";
    CHECK_FALSE(std::filesystem::exists(directory / "CMakeLists.txt"));
    std::ifstream script(directory / "src" / "CMakeLists.txt");
    const std::string text{std::istreambuf_iterator<char>(script), std::istreambuf_iterator<char>()};
    CHECK(text.find("project(My_Game LANGUAGES CXX)") != std::string::npos);
    CHECK(text.find("spot_add_game_module(game ${sources})") != std::string::npos);
    CHECK(text.find("src/") == std::string::npos);
    CHECK(std::filesystem::is_regular_file(directory / "src" / "game.cpp"));

    const auto project = Spot::Editor::EditorProject::Open(*file, nullptr);
    REQUIRE(project != nullptr);
    CHECK(project->GetModules().HasCode());
    const auto scene = project->GetAssets().InstantiateScene(project->GetProject().GetSettings().startupScene);
    REQUIRE(scene != nullptr);
    const auto* rotator = scene->FindEntity("Cube")->GetComponent<Spot::MissingComponent>();
    REQUIRE(rotator != nullptr);
    CHECK(rotator->GetTypeName() == "Rotator");
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

TEST_CASE("Projects of older editors get the current layout when they open", "[editor][project]") {
    const TempDirectory temp;
    auto project = Spot::Project::Create(temp.GetPath(), {.name = "Old"});
    REQUIRE(project);
    std::ofstream(temp.GetPath() / "CMakeLists.txt")
        << "file(GLOB_RECURSE sources CONFIGURE_DEPENDS src/*.cpp src/*.h)\n";
    std::filesystem::create_directories(temp.GetPath() / "src");
    std::ofstream(temp.GetPath() / "src" / "game.cpp") << "// The game\n";
    std::ofstream(temp.GetPath() / ".gitignore") << "/.spot/";

    CHECK(Spot::Editor::UpdateProjectLayout(*project));

    CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "CMakeLists.txt"));
    std::ifstream script(temp.GetPath() / "src" / "CMakeLists.txt");
    const std::string text{std::istreambuf_iterator<char>(script), std::istreambuf_iterator<char>()};
    CHECK(text == "file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp *.h)\n");
    std::ifstream ignore(temp.GetPath() / ".gitignore", std::ios::binary);
    const std::string ignored{std::istreambuf_iterator<char>(ignore), std::istreambuf_iterator<char>()};
    CHECK(ignored == "/.spot/\n# Games exported from the project\n/builds/\n");

    // Once is enough.
    CHECK_FALSE(Spot::Editor::UpdateProjectLayout(*project));
    Spot::Editor::ModuleHost host(*project, {});
    CHECK(host.HasCode());
}

TEST_CASE("New components need C++ class names that no type or file has", "[editor][project]") {
    const TempDirectory temp;
    const auto project = Spot::Project::Create(temp.GetPath(), {.name = "Names"});
    REQUIRE(project);
    using Spot::Editor::ValidateComponentName;
    CHECK(ValidateComponentName(*project, "", "Player").empty());
    CHECK(ValidateComponentName(*project, "", "_Player2").empty());
    CHECK_FALSE(ValidateComponentName(*project, "", "").empty());
    CHECK_FALSE(ValidateComponentName(*project, "", "2Player").empty());
    CHECK_FALSE(ValidateComponentName(*project, "", "Big Player").empty());
    CHECK_FALSE(ValidateComponentName(*project, "", "class").empty());
    // The engine's own types keep their names.
    CHECK_FALSE(ValidateComponentName(*project, "", "Camera").empty());

    // A project without code gets what building it takes, beside the component's files.
    const auto files = Spot::Editor::CreateComponentCode(*project, "actors", "Player");
    REQUIRE(files);
    CHECK(*files == std::vector<std::string>{"actors/Player.h", "actors/Player.cpp"});
    const auto code = project->GetCodeDirectory();
    CHECK(std::filesystem::is_regular_file(code / "CMakeLists.txt"));
    CHECK(std::filesystem::is_regular_file(code / "game.cpp"));
    std::ifstream source(code / "actors" / "Player.cpp");
    const std::string text{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
    CHECK(text.find("SPOT_COMPONENT(Player);") != std::string::npos);
    CHECK(text.find("void Player::OnUpdate(") != std::string::npos);

    CHECK_FALSE(ValidateComponentName(*project, "actors", "Player").empty());
    CHECK(ValidateComponentName(*project, "", "Player").empty());
    CHECK_FALSE(Spot::Editor::CreateComponentCode(*project, "actors", "Player"));
}
