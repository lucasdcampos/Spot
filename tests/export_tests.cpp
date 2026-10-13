#include "temp_directory.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>

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
