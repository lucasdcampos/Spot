// The manual's first game (docs/manual/getting_started/first_game.md), played as its reader would: holding D walks
// the player through the coins.
#include "log_capture.h"
#include "registered_assets.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

TEST_CASE("The first game of the manual collects its coins", "[examples][scripting]") {
    auto log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    const std::filesystem::path cooked = SPOT_FIRST_GAME_EXAMPLE_DIR;
    const auto game = Spot::GameSettings::LoadFromFile(cooked / Spot::GameSettings::FileName);
    REQUIRE(game);
    Spot::Tests::RegisteredAssetManager assets{cooked / Spot::Utf8ToPath(game->importDirectory)};
    Spot::InputState input;
    Spot::InputActionMap actions;
    game->sections.input.Apply(actions);
    auto* previousInput = Spot::Input::GetState();
    auto* previousActions = Spot::Input::GetActionMap();
    auto* previousAssets = Spot::Assets::GetManager();
    Spot::Input::SetState(&input);
    Spot::Input::SetActionMap(&actions);
    Spot::Detail::SetAssetManager(&assets);

    {
        Spot::Scene scene;
        REQUIRE(scene.LoadFromFile(std::filesystem::path(SPOT_FIRST_GAME_SOURCE_DIR) / "assets/scenes/Main.scene",
                                   &assets));
        input.OnEvent(Spot::KeyPressedEvent{Spot::Key::D, false});
        for (int frame = 0; frame < 120; ++frame) {
            actions.Update(input);
            scene.Update(Spot::Scene::DefaultFixedTimeStep);
            input.BeginFrame();
        }
        REQUIRE(scene.FindEntity("Player"));
        CHECK(scene.FindEntity("Player")->GetTransform().GetPosition().x > 6.0f);
        CHECK_FALSE(scene.FindEntity("Coin 1"));
        CHECK_FALSE(scene.FindEntity("Coin 3"));
    }

    Spot::Input::SetState(previousInput);
    Spot::Input::SetActionMap(previousActions);
    Spot::Detail::SetAssetManager(previousAssets);
    CHECK(std::ranges::any_of(log->GetRecords(), [](const auto& record) {
        return record.message.find("You collected every coin!") != std::string::npos;
    }));
}
