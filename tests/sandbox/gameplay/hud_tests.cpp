#include "gameplay_world.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/hud/hud.h"

#include <catch2/catch_test_macros.hpp>

namespace {

class StateElement final : public Gameplay::HudElement {
public:
    Spot::EntityId text = Spot::EntityId::Invalid;
    int refreshes = 0;

protected:
    void Refresh(const Gameplay::HudContext& context) override {
        ++refreshes;
        SetText(text, context.pawn ? context.pawn->GetEntity().GetName() : "No pawn");
    }
};

} // namespace

TEST_CASE("A new HUD element follows authored states and the HUD's local player", "[sandbox][gameplay][hud]") {
    GameplayTests::World world;
    auto& first = world.scene.CreateEntity("First").AddComponent<Gameplay::Pawn>();
    auto& second = world.scene.CreateEntity("Second").AddComponent<Gameplay::Pawn>();
    world.scene.CreateEntity("First controller").AddComponent<Gameplay::PlayerController>().pawn =
        first.GetEntity().GetId();
    world.scene.CreateEntity("Second controller").AddComponent<Gameplay::PlayerController>().pawn =
        second.GetEntity().GetId();
    auto& root = world.scene.CreateEntity("HUD");
    root.AddComponent<Gameplay::Hud>().playerIndex = 1;
    auto& panel = root.CreateChild("Panel");
    auto& element = panel.AddComponent<StateElement>();
    element.showWhen = {"Inspecting"};
    element.hideWhen = {"Dead"};
    auto& text = panel.CreateChild("Text").AddComponent<Spot::UiText>();
    element.text = text.GetEntity().GetId();
    first.SetState("Inspecting", true);
    world.Step();
    CHECK_FALSE(element.IsShown());
    CHECK_FALSE(panel.IsActiveSelf());
    CHECK(element.refreshes == 0);

    second.SetState("Inspecting", true);
    world.Step();
    CHECK(element.IsShown());
    CHECK(panel.IsActiveSelf());
    CHECK(text.text == "Second");
    const int before = element.refreshes;
    second.SetState("Dead", true);
    world.Step();
    CHECK_FALSE(element.IsShown());
    CHECK(element.refreshes == before);
    second.SetState("Dead", false);
    world.Step();
    CHECK(element.IsShown());
    CHECK(element.refreshes > before);
}
