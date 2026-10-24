#include "temp_directory.h"

#include <spot.h>
#include <spot_tools.h>

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
    CHECK(project->GetModuleFile() ==
          std::filesystem::absolute(directory / ".spot" / "build" / "module" /
                                    ("game" + std::string(Spot::GameLibrary::FileExtension))));
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
    CHECK(loaded->GetAssetsDirectory() == temp.GetPath() / "assets");
    CHECK(loaded->GetCodeDirectory() == temp.GetPath() / "src");
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

TEST_CASE("Projects tell their game what it is, and keep a missing startup scene missing", "[project]") {
    const TempDirectory temp;
    auto project = Spot::Project::Create(temp.GetPath(), {.name = "Game", .version = "2.0.0"});
    REQUIRE(project.has_value());
    CHECK_THAT(Spot::Project::SaveSettingsToString(project->GetSettings()),
               Catch::Matchers::ContainsSubstring("\"startupScene\": null"));
    CHECK_FALSE(Spot::Project::Load(project->GetFile())->GetSettings().startupScene.IsValid());

    project->GetSettings().startupScene = Spot::AssetId::Generate();
    const Spot::GameSettings game = project->GetGameSettings();
    CHECK(game.name == "Game");
    CHECK(game.version == "2.0.0");
    CHECK(game.startupScene == project->GetSettings().startupScene);
}

TEST_CASE("Projects store the settings sections, keep unknown ones, and give them to their game", "[project]") {
    const TempDirectory temp;
    auto project = Spot::Project::Create(temp.GetPath(), {.name = "Sections"});
    REQUIRE(project.has_value());
    project->GetSettings().sections.physics.gravity = {0.0f, -2.0f, 0.0f};
    project->GetSettings().sections.unknown["future"] = R"({"value":true})";
    REQUIRE(project->Save());

    const auto loaded = Spot::Project::Load(project->GetFile());
    REQUIRE(loaded.has_value());
    CHECK(loaded->GetSettings().sections.physics.gravity == Spot::Vector3{0.0f, -2.0f, 0.0f});
    CHECK(loaded->GetSettings().sections.unknown.at("future") == R"({"value":true})");

    const Spot::GameSettings game = loaded->GetGameSettings();
    CHECK(game.sections.physics.gravity == Spot::Vector3{0.0f, -2.0f, 0.0f});
    CHECK(game.sections.unknown.contains("future"));
}
