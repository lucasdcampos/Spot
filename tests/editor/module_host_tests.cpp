// Builds projects' code into game modules with CMake and the compiler that built the editor, which takes a while, so
// these tests have an executable of their own.
#include "log_capture.h"
#include "temp_directory.h"

#include "modules/module_host.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_approx.hpp>
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
using Spot::CodeBuildState;

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

class Game final : public Spot::GameCodeModule {
protected:
    void OnStart() override {
        GameCodeModule::OnStart();
        Spot::RegisterComponent<Rotator>("Rotator");
        Spot::RegisterComponent<Pulse>("Pulse");
    }
};

} // namespace

SPOT_GAME_MODULE(Game)
)";

constexpr std::string_view BlinkComponent = R"(#include <spot.h>

class Blink final : public Spot::Component {};

SPOT_COMPONENT(Blink);
)";

constexpr std::string_view BrokenCode = "#include <spot.h>\n\nint Broken( {\n";

// The game's main() for tests: without graphics, which machines that run them may not have, and only for a few frames.
constexpr std::string_view TestMain = R"(#include <spot.h>

namespace {

class TestGame final : public Spot::Application {
public:
    TestGame() : Application({.game = Spot::GameSettings::LoadBesideExecutable(), .graphics = {.enabled = false}}) {
        AddModule<Spot::RenderModule>();
        AddModule(Spot::CreateGameModule());
    }

protected:
    void OnUpdate(float) override {
        if (const Spot::Scene* scene = GetScene(); scene && scene->FindEntity("Cube") && !m_toldOfCube) {
            m_toldOfCube = true;
            Spot::Log::Info("The cube is there");
        }
        if (++m_frames == 5) {
            Quit();
        }
    }

private:
    int m_frames = 0;
    bool m_toldOfCube = false;
};

} // namespace

int main() {
    return TestGame().Run();
}
)";

// Runs the game until it exits, and returns what it logged.
std::vector<std::string> RunGame(const std::filesystem::path& executable) {
    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});
    Spot::GameRunner game;
    REQUIRE(game.Start(executable));
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
    return messages;
}

bool Contains(const std::vector<std::string>& messages, std::string_view text) {
    return std::ranges::any_of(messages,
                               [&](const std::string& message) { return message.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("Build output is told apart: errors, warnings and the rest", "[editor][module]") {
    using Spot::CodeBuild;
    using Spot::LogLevel;

    CHECK(CodeBuild::ClassifyLine(R"(C:\game\src\game.cpp(12,5): error C2065: 'x': undeclared identifier)") ==
          LogLevel::Error);
    CHECK(CodeBuild::ClassifyLine("src/game.cpp:12:5: error: 'x' was not declared in this scope") == LogLevel::Error);
    CHECK(CodeBuild::ClassifyLine("src/game.cpp:3:10: fatal error: missing.h: No such file or directory") ==
          LogLevel::Error);
    CHECK(CodeBuild::ClassifyLine("game.obj : error LNK2019: unresolved external symbol Foo") == LogLevel::Error);
    CHECK(CodeBuild::ClassifyLine("game.cpp:(.text+0x1f): undefined reference to `Foo()'") == LogLevel::Error);
    CHECK(CodeBuild::ClassifyLine("CMake Error at CMakeLists.txt:5 (find_package):") == LogLevel::Error);
    CHECK(CodeBuild::ClassifyLine("src/game.cpp:7:9: warning: unused variable 'y' [-Wunused-variable]") ==
          LogLevel::Warning);
    CHECK(CodeBuild::ClassifyLine(R"(C:\game\src\game.cpp(7,9): warning C4189: 'y': local variable is unused)") ==
          LogLevel::Warning);
    CHECK(CodeBuild::ClassifyLine("  game.cpp") == LogLevel::Trace);
    CHECK(CodeBuild::ClassifyLine("-- Configuring done (0.2s)") == LogLevel::Trace);
    CHECK(CodeBuild::ClassifyLine("[2/3] Building CXX object CMakeFiles/game.dir/src/game.cpp.obj") == LogLevel::Trace);
}

TEST_CASE("Components made from the editor build, without warnings, in projects that had no code", "[editor][module]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Plain", Spot::ProjectTemplate::Empty);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    REQUIRE(Spot::CreateComponentCode(*project, "actors", "Player"));

    const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({sink});
    Spot::Editor::ModuleHost host(*project, {.watchInterval = 0ms});
    REQUIRE(host.HasCode());
    CHECK_FALSE(host.LoadBuilt());
    WaitForBuild(host);
    REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
    CHECK(host.GetBuild().GetErrorCount() == 0);
    // The build tool may warn of the temporary folder the test builds in, but the compiler of nothing in the code.
    for (const Spot::Tests::CapturedRecord& record : sink->GetRecords()) {
        if (record.level >= Spot::LogLevel::Warning) {
            CHECK(record.message.find("Player") == std::string::npos);
            CHECK(record.message.find("game.cpp") == std::string::npos);
        }
    }
    REQUIRE(host.SwapModule());
    CHECK(GetTypes(*host.GetModule()) == std::vector<std::string>{"Player"});
    CHECK(Spot::CreateComponent("Player"));
}

TEST_CASE("A project's code builds into a module that loads, and loads again when the code changes",
          "[editor][module]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Coded", Spot::ProjectTemplate::Code3D);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    const auto code = project->GetDirectory() / "src" / "game.cpp";
    {
        Spot::Editor::ModuleHost host(*project, {.watchInterval = 0ms});
        REQUIRE(host.HasCode());
        Spot::Scene scene;

        // Never built, so there is nothing to load, and it builds, configuring first.
        CHECK_FALSE(host.LoadBuilt());
        CHECK(host.GetBuild().IsRunning());
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
        CHECK(host.GetBuild().GetErrorCount() == 0);
        const Spot::CodeBuildPhases& phases = host.GetBuild().GetPhases();
        CHECK(phases.configure.count() > 0.0f);
        CHECK(phases.compile.count() > 0.0f);
        CHECK((phases.configure + phases.compile + phases.link).count() ==
              Catch::Approx(host.GetBuild().GetDuration().count()).margin(0.01f));
        REQUIRE(host.HasNewModule());
        REQUIRE(host.SwapModule());
        CHECK(host.GetLoadDuration().count() > 0.0f);
        REQUIRE(host.GetModule());
        CHECK(GetTypes(*host.GetModule()) == std::vector<std::string>{"Rotator"});

        Spot::Entity& cube = scene.CreateEntity("Cube");
        REQUIRE(Spot::AddComponentFromString(cube, R"({"type":"Rotator","enabled":true,"data":{"speed":0.25}})"));
        CHECK_FALSE(cube.GetComponent<Spot::MissingComponent>());

        // A build that changes nothing has nothing new to load.
        host.Build();
        WaitForBuild(host);
        CHECK(host.GetBuild().GetState() == CodeBuildState::Succeeded);
        CHECK_FALSE(host.HasNewModule());

        // Changed code builds by itself; the new module replaces the old one while the scene is saved and cleared.
        WriteCode(code, TwoComponents);
        host.Update();
        CHECK(host.GetBuild().IsRunning());
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
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

        // A file of code added builds on the first try: the build directory is configured again for it.
        WriteCode(code.parent_path() / "blink.cpp", BlinkComponent);
        host.Update();
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
        REQUIRE(host.HasNewModule());
        scene.Clear();
        REQUIRE(host.SwapModule());
        CHECK(GetTypes(*host.GetModule()) == std::vector<std::string>{"Blink", "Pulse", "Rotator"});
        std::filesystem::remove(code.parent_path() / "blink.cpp");

        // Code that does not build leaves the module that was loaded.
        WriteCode(code, BrokenCode);
        host.Update();
        WaitForBuild(host);
        CHECK(host.GetBuild().GetState() == CodeBuildState::Failed);
        CHECK(host.GetBuild().GetErrorCount() > 0);
        CHECK_FALSE(host.HasNewModule());
        CHECK(host.GetModule());

        // The module's components go before it.
        scene.Clear();
    }
    CHECK_FALSE(Spot::CreateComponent("Rotator"));
}

TEST_CASE("Projects build their game's executable, which runs the startup scene with the game's code, and exports",
          "[editor][module][game]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Played", Spot::ProjectTemplate::Code3D);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    std::ofstream(project->GetCodeDirectory() / "main.cpp", std::ios::binary) << TestMain;
    {
        Spot::Editor::ModuleHost host(*project, {.watchInterval = 0ms});
        // Builds on change make the module alone; running the game builds its executable too.
        host.Build();
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
        CHECK_FALSE(std::filesystem::exists(project->GetGameExecutable()));
        host.Build(Spot::Editor::ModuleHost::BuildScope::Game);
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
    }
    Spot::ImportProjectAssets(*project);
    const auto launch = Spot::PrepareGameRun(*project);
    REQUIRE(launch);
    CHECK(launch->executable == project->GetGameExecutable());
    CHECK(launch->arguments.empty());

    const std::vector<std::string> run = RunGame(launch->executable);
    CHECK(Contains(run, "The cube is there"));
    CHECK(Contains(run, "The game exited"));
    // The cube's component comes from the game's code, which its executable has.
    CHECK_FALSE(Contains(run, "not registered"));

    // Exported, the game runs from its own folder, named after the project, without the project.
    const std::filesystem::path exported = temp.GetPath() / "exported";
    REQUIRE(Spot::ExportGame(*project, exported));
    std::filesystem::path executable = exported / "Played";
    executable += project->GetGameExecutable().extension();
    REQUIRE(std::filesystem::is_regular_file(executable));
    CHECK(std::filesystem::is_regular_file(exported / Spot::GameSettings::FileName));
    CHECK(std::filesystem::is_directory(exported / "imported"));
    std::filesystem::remove_all(project->GetImportDirectory());

    const std::vector<std::string> shipped = RunGame(executable);
    CHECK(Contains(shipped, "The cube is there"));
    CHECK_FALSE(Contains(shipped, "not registered"));
}

TEST_CASE("Modules built for another editor are not loaded, and are built again", "[editor][module]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Shared", Spot::ProjectTemplate::Code3D);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    {
        Spot::Editor::ModuleHost host(*project, {.watchInterval = 0ms});
        host.Build();
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
    }

    // As an editor of another configuration finds the module.
    Spot::Toolchain other = Spot::Toolchain::GetBuilt();
    other.configuration = other.configuration == "Debug" ? "Release" : "Debug";
    Spot::Editor::ModuleHost host(*project, {.toolchain = other, .watchInterval = 0ms});
    CHECK_FALSE(host.GetBuild().IsConfigured());
    CHECK_FALSE(host.LoadBuilt());
    CHECK_FALSE(host.GetModule());
    CHECK(host.GetBuild().IsRunning());
    // Stopped here: building for the other configuration needs that configuration of the engine.
}

TEST_CASE("Modules built before the engine changed are built again before they load", "[editor][module]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Engine", Spot::ProjectTemplate::Code3D);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    {
        Spot::Editor::ModuleHost host(*project, {.watchInterval = 0ms});
        host.Build();
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
    }

    // As the engine is built again after the module, here without a change its headers show, so the build finds
    // nothing to do.
    Spot::Toolchain rebuilt = Spot::Toolchain::GetBuilt();
    rebuilt.engineLibrary = temp.GetPath() / "engine.dll";
    std::this_thread::sleep_for(1100ms);
    std::ofstream(rebuilt.engineLibrary, std::ios::binary) << "engine";
    {
        Spot::Editor::ModuleHost host(*project, {.toolchain = rebuilt, .watchInterval = 0ms});
        CHECK_FALSE(host.LoadBuilt());
        CHECK_FALSE(host.GetModule());
        CHECK(host.GetBuild().IsRunning());
        WaitForBuild(host);
        REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
        REQUIRE(host.HasNewModule());
        CHECK(host.SwapModule());
    }

    // Built for this engine now, it loads at once.
    Spot::Editor::ModuleHost host(*project, {.toolchain = rebuilt, .watchInterval = 0ms});
    CHECK(host.LoadBuilt());
    CHECK_FALSE(host.GetBuild().IsRunning());
}

TEST_CASE("New source files are in the first build after they are added", "[editor][module]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Grown", Spot::ProjectTemplate::Code3D);
    REQUIRE(file);
    const auto project = Spot::Project::Load(*file);
    REQUIRE(project);
    Spot::Editor::ModuleHost host(*project, {.watchInterval = 0ms});
    host.Build();
    WaitForBuild(host);
    REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);

    WriteCode(project->GetDirectory() / "src" / "pulse.cpp", R"(#include <spot.h>

class Pulse final : public Spot::Component {};

SPOT_COMPONENT(Pulse);
)");
    host.Build();
    WaitForBuild(host);
    REQUIRE(host.GetBuild().GetState() == CodeBuildState::Succeeded);
    REQUIRE(host.SwapModule());
    CHECK(GetTypes(*host.GetModule()) == std::vector<std::string>{"Pulse", "Rotator"});
}
