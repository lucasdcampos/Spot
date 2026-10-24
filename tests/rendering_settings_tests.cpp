#include <spot.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Runtime rendering changes preserve game input rebinds and project defaults", "[ui][settings]") {
    Spot::GameSettings game;
    game.sections.rendering.msaaSamples = 4;
    Spot::Application app({.graphics = {.enabled = false}});
    app.SetGameSettings(game);
    app.GetInputActions().Add("Jump", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::Space));
    app.GetInputActions().SetDefaults();
    REQUIRE(app.GetInputActions().Rebind("Jump", 0, Spot::InputControl::Of(Spot::Key::J)));
    const auto revision = app.GetRenderingRevision();
    auto rendering = app.GetRenderingSettings();
    rendering.msaaSamples = 1;
    rendering.shadowMapSize = 0;
    app.SetRenderingSettings(rendering);
    CHECK(app.GetRenderingRevision() > revision);
    CHECK(app.GetRenderingSettings().msaaSamples == 1);
    CHECK(app.GetGameSettings()->sections.rendering.msaaSamples == 4);
    CHECK(app.GetInputActions().IsRebound("Jump"));
    CHECK(app.GetInputActions().Find("Jump")->bindings.front().control == Spot::InputControl::Of(Spot::Key::J));
}
