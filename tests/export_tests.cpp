#include "temp_directory.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("Game exports include versioned runtime libraries and relative settings", "[tools][export]") {
    TempDirectory temp;
    const auto created = Spot::CreateProject(temp.GetPath(), "Shipped", Spot::ProjectTemplate::Code3D);
    REQUIRE(created);
    const auto project = Spot::Project::Load(*created);
    REQUIRE(project);
    std::filesystem::create_directories(project->GetGameExecutable().parent_path());
    std::ofstream(project->GetGameExecutable()) << "Game fixture";
    std::ofstream(project->GetGameExecutable().parent_path() / "libstdc++.so.6") << "Runtime fixture";
    std::ofstream(project->GetGameExecutable().parent_path() / "unrelated.txt") << "Build-only fixture";
    Spot::ImportProjectAssets(*project);
    const auto exported = temp.GetPath() / "export";
    REQUIRE(Spot::ExportGame(*project, exported));
    CHECK(std::filesystem::is_regular_file(exported / "libstdc++.so.6"));
    CHECK_FALSE(std::filesystem::exists(exported / "unrelated.txt"));
    const auto settings = Spot::GameSettings::LoadFromFile(exported / Spot::GameSettings::FileName);
    REQUIRE(settings);
    CHECK_FALSE(std::filesystem::path(settings->importDirectory).is_absolute());
    CHECK(std::filesystem::is_regular_file(exported / settings->importDirectory / "manifest.json"));
}

TEST_CASE("Game exports reject destinations that would delete project inputs", "[tools][export]") {
    TempDirectory temp;
    const auto created = Spot::CreateProject(temp.GetPath(), "Preserved", Spot::ProjectTemplate::Code3D);
    REQUIRE(created);
    const auto project = Spot::Project::Load(*created);
    REQUIRE(project);
    std::filesystem::create_directories(project->GetGameExecutable().parent_path());
    std::ofstream(project->GetGameExecutable()) << "Game fixture";
    Spot::ImportProjectAssets(*project);
    for (const auto& destination : {project->GetDirectory(), project->GetAssetsDirectory(), project->GetCodeDirectory(),
                                    project->GetBuildDirectory(), project->GetImportDirectory(), temp.GetPath()}) {
        CHECK_FALSE(Spot::ExportGame(*project, destination));
        CHECK(std::filesystem::is_regular_file(project->GetFile()));
        CHECK(std::filesystem::is_regular_file(project->GetGameExecutable()));
        CHECK(std::filesystem::is_regular_file(project->GetImportDirectory() / "manifest.json"));
    }
}

namespace {

// A player with the engine library beside it, as the tools' build makes them.
std::filesystem::path MakePlayerFixture(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto player = directory / "spot_player.exe";
    std::ofstream(player) << "Player fixture";
    std::ofstream(directory / "spot_shared.dll") << "Engine fixture";
    return player;
}

} // namespace

TEST_CASE("Games without code run with the player, told where their settings are", "[tools][export]") {
    TempDirectory temp;
    const auto created =
        Spot::CreateProject(temp.GetPath(), "Scripted", Spot::ProjectTemplate::Basic3D, Spot::ProjectCode::None);
    REQUIRE(created);
    const auto project = Spot::Project::Load(*created);
    REQUIRE(project);
    const auto player = MakePlayerFixture(temp.GetPath() / "player");
    CHECK(Spot::GetGameExecutable(*project, player) == player);

    const auto launch = Spot::PrepareGameRun(*project, player);
    REQUIRE(launch);
    CHECK(launch->executable == player);
    REQUIRE(launch->arguments.size() == 2);
    CHECK(launch->arguments[0] == "--game");
    const std::filesystem::path folder = Spot::Utf8ToPath(launch->arguments[1]);
    // Nothing is written beside the player, which every game without code shares.
    CHECK_FALSE(std::filesystem::exists(player.parent_path() / Spot::GameSettings::FileName));
    const auto settings = Spot::GameSettings::LoadFromFile(folder / Spot::GameSettings::FileName);
    REQUIRE(settings);
    CHECK(settings->name == "Scripted");
    CHECK(settings->startupScene == project->GetSettings().startupScene);
    CHECK(Spot::Utf8ToPath(settings->importDirectory) == project->GetImportDirectory());

    CHECK_FALSE(Spot::PrepareGameRun(*project, temp.GetPath() / "missing" / "spot_player.exe"));
}

TEST_CASE("Games without code ship the player, named after the game, with the engine beside it", "[tools][export]") {
    TempDirectory temp;
    const auto created =
        Spot::CreateProject(temp.GetPath(), "Scripted", Spot::ProjectTemplate::Basic3D, Spot::ProjectCode::None);
    REQUIRE(created);
    const auto project = Spot::Project::Load(*created);
    REQUIRE(project);
    const auto player = MakePlayerFixture(temp.GetPath() / "player");
    Spot::ImportProjectAssets(*project);

    const auto exported = temp.GetPath() / "export";
    REQUIRE(Spot::ExportGame(*project, exported, player));

    CHECK(std::filesystem::is_regular_file(exported / "Scripted.exe"));
    CHECK(std::filesystem::is_regular_file(exported / "spot_shared.dll"));
    const auto settings = Spot::GameSettings::LoadFromFile(exported / Spot::GameSettings::FileName);
    REQUIRE(settings);
    CHECK(std::filesystem::is_regular_file(exported / settings->importDirectory / "manifest.json"));
    CHECK_FALSE(std::filesystem::exists(project->GetCodeDirectory()));
}

TEST_CASE("The player refuses a folder without a game", "[tools][export]") {
    TempDirectory temp;
    const std::vector<std::string> command{SPOT_PLAYER, "--game", Spot::PathToUtf8(temp.GetPath() / "nothing")};
    const auto process = Spot::Process::Start(command);
    REQUIRE(process);
    CHECK(process->Wait() != 0);
    const auto output = process->TakeOutput();
    CHECK(std::ranges::any_of(
        output, [](const std::string& line) { return line.find("There is no game") != std::string::npos; }));
}
