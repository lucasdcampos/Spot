// Builds projects' code into game modules with CMake and the compiler that built the editor, which takes a while, so
// these tests have an executable of their own.
#include "log_capture.h"
#include "temp_directory.h"

#include "modules/module_build.h"
#include "modules/module_host.h"
#include "play/game_runner.h"
#include "project/project_templates.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using Spot::Editor::ModuleBuildState;

void WaitForBuild(Spot::Editor::ModuleHost& host) {
    while (host.GetBuild().IsRunning()) {
        std::this_thread::sleep_for(20ms);
        host.Update();
    }
}

// Make, as some generators build with, tells changed files by their times to the second, so code saved in the
// second of the last build would look built; people take longer than that between builds.
void WriteCode(const std::filesystem::path& file, std::string_view code) {
    std::this_thread::sleep_for(1100ms);
    std::ofstream(file, std::ios::binary) << code;
}

std::vector<std::string> GetTypes(const Spot::GameModule& module) {
    return {module.GetComponentTypes().begin(), module.GetComponentTypes().end()};
}

constexpr std::string_view TwoComponents = R"(#include <spot.h>

namespace {

class Rotator final : public Spot::Component {
public:
    float speed = 1.0f;

protected:
    void Serialize(Spot::Archive& archive) override { archive.Field("speed", speed); }
};

class Pulse final : public Spot::Component {};

void RegisterGame() {
    Spot::RegisterComponent<Rotator>("Rotator");
    Spot::RegisterComponent<Pulse>("Pulse");
}

} // namespace

SPOT_GAME_MODULE(RegisterGame)
)";

constexpr std::string_view BrokenCode = "#include <spot.h>\n\nint Broken( {\n";

} // namespace

TEST_CASE("Build output is told apart: errors, warnings and the rest", "[editor][module]") {
    using Spot::LogLevel;
    using Spot::Editor::ModuleBuild;

    CHECK(ModuleBuild::ClassifyLine(R"(C:\game\src\game.cpp(12,5): error C2065: 'x': undeclared identifier)") ==
          LogLevel::Error);
    CHECK(ModuleBuild::ClassifyLine("src/game.cpp:12:5: error: 'x' was not declared in this scope") == LogLevel::Error);
    CHECK(ModuleBuild::ClassifyLine("src/game.cpp:3:10: fatal error: missing.h: No such file or directory") ==
          LogLevel::Error);
    CHECK(ModuleBuild::ClassifyLine("game.obj : error LNK2019: unresolved external symbol Foo") == LogLevel::Error);
    CHECK(ModuleBuild::ClassifyLine("game.cpp:(.text+0x1f): undefined reference to `Foo()'") == LogLevel::Error);
    CHECK(ModuleBuild::ClassifyLine("CMake Error at CMakeLists.txt:5 (find_package):") == LogLevel::Error);
    CHECK(ModuleBuild::ClassifyLine("src/game.cpp:7:9: warning: unused variable 'y' [-Wunused-variable]") ==
          LogLevel::Warning);
    CHECK(ModuleBuild::ClassifyLine(R"(C:\game\src\game.cpp(7,9): warning C4189: 'y': local variable is unused)") ==
          LogLevel::Warning);
    CHECK(ModuleBuild::ClassifyLine("  game.cpp") == LogLevel::Trace);
    CHECK(ModuleBuild::ClassifyLine("-- Configuring done (0.2s)") == LogLevel::Trace);
    CHECK(ModuleBuild::ClassifyLine("[2/3] Building CXX object CMakeFiles/game.dir/src/game.cpp.obj") ==
          LogLevel::Trace);
}

TEST_CASE("A project's code builds into a module that loads, and loads again when the code changes",
          "[editor][module]") {
    const TempDirectory temp;
    const auto file = Spot::Editor::CreateProject(temp.GetPath(), "Coded", Spot::Editor::ProjectTemplate::Code3D);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    const auto code = project->GetDirectory() / "src" / "game.cpp";
    {
        Spot::Editor::ModuleHost host(*project, {.watchInterval = 0ms});
        REQUIRE(host.HasCode());
        Spot::Scene scene;

        // Never built, so there is nothing to load, and it builds.
        CHECK_FALSE(host.LoadBuilt());
        CHECK(host.GetBuild().IsRunning());
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == ModuleBuildState::Succeeded);
        CHECK(host.GetBuild().GetErrorCount() == 0);
        REQUIRE(host.HasNewModule());
        REQUIRE(host.SwapModule());
        REQUIRE(host.GetModule());
        CHECK(GetTypes(*host.GetModule()) == std::vector<std::string>{"Rotator"});

        Spot::Entity& cube = scene.CreateEntity("Cube");
        REQUIRE(Spot::AddComponentFromString(cube, R"({"type":"Rotator","enabled":true,"data":{"speed":0.25}})"));
        CHECK_FALSE(cube.GetComponent<Spot::MissingComponent>());

        // A build that changes nothing has nothing new to load.
        host.Build();
        WaitForBuild(host);
        CHECK(host.GetBuild().GetState() == ModuleBuildState::Succeeded);
        CHECK_FALSE(host.HasNewModule());

        // Changed code builds by itself; the new module replaces the old one while the scene is saved and cleared.
        WriteCode(code, TwoComponents);
        host.Update();
        CHECK(host.GetBuild().IsRunning());
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == ModuleBuildState::Succeeded);
        REQUIRE(host.HasNewModule());
        const std::string saved = scene.SaveToString();
        scene.Clear();
        REQUIRE(host.SwapModule());
        CHECK(GetTypes(*host.GetModule()) == std::vector<std::string>{"Pulse", "Rotator"});
        REQUIRE(scene.Instantiate(saved, {.keepIds = true, .keepExternalReferences = true}).size() == 1);
        const Spot::Entity* reloaded = scene.FindEntity("Cube");
        REQUIRE(reloaded);
        CHECK_FALSE(reloaded->GetComponent<Spot::MissingComponent>());
        CHECK(scene.SaveToString() == saved);
        CHECK(Spot::CreateComponent("Pulse"));

        // Code that does not build leaves the module that was loaded.
        WriteCode(code, BrokenCode);
        host.Update();
        WaitForBuild(host);
        CHECK(host.GetBuild().GetState() == ModuleBuildState::Failed);
        CHECK(host.GetBuild().GetErrorCount() > 0);
        CHECK_FALSE(host.HasNewModule());
        CHECK(host.GetModule());

        // The module's components go before it.
        scene.Clear();
    }
    CHECK_FALSE(Spot::CreateComponent("Rotator"));
}

TEST_CASE("The player runs a project's startup scene with the game's code", "[editor][module][player]") {
    const TempDirectory temp;
    const auto file = Spot::Editor::CreateProject(temp.GetPath(), "Played", Spot::Editor::ProjectTemplate::Code3D);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    {
        Spot::Editor::ModuleHost host(*project, {.watchInterval = 0ms});
        host.Build();
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == ModuleBuildState::Succeeded);
    }

    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});
    Spot::Editor::GameRunner game;
    const std::array options{std::string("--no-graphics"), std::string("--frames"), std::string("5")};
    REQUIRE(game.Start(*file, options));
    while (game.IsRunning()) {
        std::this_thread::sleep_for(20ms);
        game.Update();
    }
    game.Update();

    CHECK(game.GetExitCode() == 0);
    std::vector<std::string> messages;
    for (const Spot::Tests::CapturedRecord& record : capture->GetRecords()) {
        if (record.loggerName == "Game") {
            messages.push_back(record.message);
        }
    }
    const auto logged = [&](std::string_view text) {
        return std::ranges::any_of(messages,
                                   [&](const std::string& message) { return message.find(text) != std::string::npos; });
    };
    CHECK(logged("Spot: Loaded the game module"));
    CHECK(logged("Player: Running 'Played'"));
    CHECK(logged("The game exited"));
    // The cube's component comes from the game's code, which the player has.
    CHECK_FALSE(logged("not registered"));
}
