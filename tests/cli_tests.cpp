// The tools' command line, run as people and scripts run it. Building games with it is tested with the editor's
// modules, which build them the same way.
#include "temp_directory.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Result {
    int exitCode = -1;
    std::vector<std::string> output;

    bool Says(std::string_view text) const {
        return std::ranges::any_of(output, [&](const std::string& line) { return line.find(text) != line.npos; });
    }
};

Result RunSpot(std::initializer_list<std::string> arguments) {
    std::vector<std::string> command{SPOT_CLI};
    command.insert(command.end(), arguments.begin(), arguments.end());
    const auto process = Spot::Process::Start(command);
    REQUIRE(process);
    Result result;
    result.exitCode = process->Wait();
    result.output = process->TakeOutput();
    return result;
}

} // namespace

TEST_CASE("The command line tells how it is used, and refuses what it does not know", "[cli]") {
    const Result help = RunSpot({"help"});
    CHECK(help.exitCode == 0);
    CHECK(help.Says("Usage: spot <command>"));

    const Result unknown = RunSpot({"conjure"});
    CHECK(unknown.exitCode != 0);
    CHECK(unknown.Says("There is no command 'conjure'"));
}

TEST_CASE("The command line creates projects and cooks their games", "[cli]") {
    const TempDirectory temp;
    const std::string location = Spot::PathToUtf8(temp.GetPath());
    REQUIRE(RunSpot({"new", "Cooked", "--path", location}).exitCode == 0);
    const std::filesystem::path directory = temp.GetPath() / "Cooked";
    CHECK(std::filesystem::is_regular_file(directory / "Cooked.spot"));
    CHECK(std::filesystem::is_regular_file(directory / "src" / "main.cpp"));
    CHECK(RunSpot({"new", "Cooked", "--path", location}).exitCode != 0);

    // Cooked for an executable in the folder, the game is told where its assets are and what it starts with.
    const std::filesystem::path out = temp.GetPath() / "out";
    REQUIRE(RunSpot({"cook", Spot::PathToUtf8(directory), "--out", Spot::PathToUtf8(out)}).exitCode == 0);
    const auto settings = Spot::GameSettings::LoadFromFile(out / Spot::GameSettings::FileName);
    REQUIRE(settings);
    const auto project = Spot::Project::Load(directory / "Cooked.spot");
    REQUIRE(project);
    CHECK(settings->name == "Cooked");
    CHECK(settings->startupScene == project->GetSettings().startupScene);
    CHECK(std::filesystem::is_regular_file(out / "imported" / "manifest.json"));

    // Without a folder, it imports into the project's own imports, as the editor does.
    REQUIRE(RunSpot({"cook", Spot::PathToUtf8(directory)}).exitCode == 0);
    CHECK(std::filesystem::is_regular_file(project->GetImportDirectory() / "manifest.json"));
    CHECK(RunSpot({"cook", Spot::PathToUtf8(temp.GetPath())}).exitCode != 0);
}

TEST_CASE("The command line creates projects without code, which build nothing", "[cli]") {
    const TempDirectory temp;
    const std::string location = Spot::PathToUtf8(temp.GetPath());
    REQUIRE(RunSpot({"new", "Scripted", "--path", location, "--code", "none"}).exitCode == 0);
    const std::filesystem::path directory = temp.GetPath() / "Scripted";
    CHECK(std::filesystem::is_regular_file(directory / "Scripted.spot"));
    CHECK_FALSE(std::filesystem::exists(directory / "src"));

    // Opening the project, as every command does, gives it no code.
    CHECK(RunSpot({"build", Spot::PathToUtf8(directory)}).exitCode == 0);
    CHECK_FALSE(std::filesystem::exists(directory / "src"));
    CHECK_FALSE(std::filesystem::exists(directory / ".spot" / "build"));

    const Result rejected =
        RunSpot({"new", "Rotating", "--path", location, "--code", "none", "--template", "component"});
    CHECK(rejected.exitCode != 0);
    CHECK(RunSpot({"new", "Other", "--path", location, "--code", "rust"}).exitCode != 0);
}
