#include "temp_directory.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("Projects are created as a file named after them beside an assets folder", "[project]") {
    const TempDirectory temp;
    const auto directory = temp.GetPath() / "My Game";

    const auto project = Spot::Project::Create(directory, {.name = "My Game"});

    REQUIRE(project.has_value());
    CHECK(std::filesystem::is_regular_file(directory / "My Game.spot"));
    CHECK(std::filesystem::is_directory(directory / "assets"));
    CHECK(project->GetFile() == std::filesystem::absolute(directory / "My Game.spot"));
    CHECK(project->GetAssetsDirectory() == std::filesystem::absolute(directory / "assets"));
    CHECK(project->GetImportDirectory() == std::filesystem::absolute(directory / ".spot" / "imported"));
}

TEST_CASE("Projects save and load their settings", "[project]") {
    const TempDirectory temp;
    const auto scene = Spot::AssetId::Generate();
    auto created = Spot::Project::Create(temp.GetPath(), {.name = "Café", .version = "1.2.3"});
    REQUIRE(created.has_value());
    created->GetSettings().startupScene = scene;
    REQUIRE(created->Save());

    const auto loaded = Spot::Project::Load(created->GetFile());

    REQUIRE(loaded.has_value());
    CHECK(loaded->GetSettings().name == "Café");
    CHECK(loaded->GetSettings().version == "1.2.3");
    CHECK(loaded->GetSettings().startupScene == scene);
    CHECK(loaded->GetSettings().assetsDirectory == "assets");
}

TEST_CASE("Project files are readable JSON with their format and engine version", "[project]") {
    const std::string text = Spot::Project::SaveSettingsToString({.name = "Game"});

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"format\": \"spot-project\""));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"engineVersion\""));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"name\": \"Game\""));
}

TEST_CASE("Projects reject files that are not projects", "[project]") {
    const TempDirectory temp;
    const auto file = temp.GetPath() / "other.spot";

    CHECK_FALSE(Spot::Project::Load(file).has_value());

    std::ofstream(file) << "{\"format\": \"spot-scene\"}";
    CHECK_FALSE(Spot::Project::Load(file).has_value());

    std::ofstream(file) << "not json";
    CHECK_FALSE(Spot::Project::Load(file).has_value());

    // Projects from newer engines may hold settings this one cannot keep.
    std::ofstream(file) << "{\"format\": \"spot-project\", \"formatVersion\": 99, \"name\": \"Future\"}";
    CHECK_FALSE(Spot::Project::Load(file).has_value());

    CHECK_FALSE(Spot::Project::Create(temp.GetPath(), {}).has_value());
}
