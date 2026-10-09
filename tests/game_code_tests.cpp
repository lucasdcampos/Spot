// The test program's own game code, as a game's executable builds its code in.
#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

namespace {

class BuiltInGameComponent final : public Spot::Component {};

class QuittingApplication final : public Spot::Application {
public:
    QuittingApplication() : Application({.graphics = {.enabled = false}, .audio = {.enabled = false}}) {}

    bool registeredAtStart = false;

protected:
    void OnStart() override {
        registeredAtStart = Spot::CreateComponent("BuiltInGameComponent") != nullptr;
        Quit();
    }
};

} // namespace

SPOT_COMPONENT(BuiltInGameComponent);

SPOT_GAME_MODULE()

TEST_CASE("Game code built into the program registers its components when the application runs", "[application]") {
    Spot::UnregisterComponent("BuiltInGameComponent");
    REQUIRE_FALSE(Spot::CreateComponent("BuiltInGameComponent"));

    QuittingApplication application;
    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.registeredAtStart);
}
