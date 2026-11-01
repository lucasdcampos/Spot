#include "gameplay_world.h"

#include "gameplay/character/character_elements.h"
#include "gameplay/chat/chat.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/health/health_elements.h"
#include "gameplay/hud/hud.h"
#include "gameplay/menus/settings_menu.h"
#include "gameplay/weapons/firearm.h"

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

TEST_CASE("Crosshair T shape and center dot can be configured independently", "[sandbox][gameplay][hud]") {
    GameplayTests::World world;
    auto& root = world.scene.CreateEntity("HUD");
    root.AddComponent<Gameplay::Hud>();
    auto& entity = root.CreateChild("Crosshair");
    auto& crosshair = entity.AddComponent<Gameplay::CrosshairElement>();
    for (const auto* name : {"Top", "Bottom", "Left", "Right", "Dot", "Dot Outline"}) {
        entity.CreateChild(name).AddComponent<Spot::UiRect>();
    }
    world.Step();
    CHECK_FALSE(entity.FindChild("Top")->IsActiveSelf());
    CHECK_FALSE(entity.FindChild("Dot")->IsActiveSelf());
    CHECK_FALSE(entity.FindChild("Dot Outline")->IsActiveSelf());
    CHECK(entity.FindChild("Bottom")->IsActiveSelf());
    CHECK(entity.FindChild("Left")->IsActiveSelf());
    CHECK(entity.FindChild("Right")->IsActiveSelf());
    crosshair.tShape = false;
    crosshair.dot = true;
    world.Step();
    CHECK(entity.FindChild("Top")->IsActiveSelf());
    CHECK(entity.FindChild("Dot")->IsActiveSelf());
    CHECK(entity.FindChild("Dot Outline")->IsActiveSelf());
    crosshair.tShape = true;
    world.Step();
    CHECK_FALSE(entity.FindChild("Top")->IsActiveSelf());
    CHECK(entity.FindChild("Dot")->IsActiveSelf());
}

TEST_CASE("Kill feed captures actor deaths and ignores inanimate names", "[sandbox][gameplay][hud]") {
    GameplayTests::World world;
    auto& actor = world.scene.CreateEntity("Actor");
    actor.AddComponent<Gameplay::Pawn>();
    actor.AddComponent<Gameplay::KillFeedSubject>();
    auto& prop = world.scene.CreateEntity("Prop");
    auto& vehicle = world.scene.CreateEntity("Vehicle");
    vehicle.AddComponent<Gameplay::Pawn>();
    Gameplay::DamageApplied death{actor.GetId(), {.type = "Bullet", .instigator = prop.GetId()}};
    auto entry = Gameplay::KillFeedElement::Describe(world.scene, death);
    CHECK(entry.victim == "Actor");
    CHECK(entry.killer.empty());
    death.target = prop.GetId();
    CHECK(Gameplay::KillFeedElement::Describe(world.scene, death).victim.empty());
    death.target = vehicle.GetId();
    CHECK(Gameplay::KillFeedElement::Describe(world.scene, death).victim.empty());
    death.target = actor.GetId();
    death.damage.instigator = actor.GetId();
    entry = Gameplay::KillFeedElement::Describe(world.scene, death);
    CHECK(entry.victim == "Actor");
    CHECK(entry.killer.empty());
    const auto& firearm = world.scene.CreateEntity("Ray Gun").AddComponent<Gameplay::Firearm>();
    death.damage.source = firearm.GetEntity().GetId();
    CHECK(Gameplay::KillFeedElement::Describe(world.scene, death).icon == firearm.icon);
}

TEST_CASE("Infinite firearm ammunition uses the numeric counter's infinity symbol", "[sandbox][gameplay][hud]") {
    GameplayTests::World world;
    auto& firearm = world.scene.CreateEntity("Ray Gun").AddComponent<Gameplay::Firearm>();
    firearm.infiniteAmmo = true;
    const auto status = firearm.DescribeStatus();
    CHECK(status.count == "\xe2\x88\x9e");
    CHECK_FALSE(status.countIsWord);
    CHECK(status.detail.empty());
    CHECK_FALSE(status.warning);
}

TEST_CASE("Singleplayer chat resolves local nickname and handles message flow", "[sandbox][gameplay][hud][chat]") {
    GameplayTests::World world;
    auto& root = world.scene.CreateEntity("HUD");
    auto& chatEntity = root.CreateChild("Chat");
    auto& chat = chatEntity.AddComponent<Gameplay::ChatElement>();
    auto& rowEntity = chatEntity.CreateChild("Row");
    auto& row = rowEntity.AddComponent<Gameplay::ChatRow>();
    auto& inputEntity = chatEntity.CreateChild("Input");
    inputEntity.AddComponent<Spot::UiInputField>();

    chat.rows = {rowEntity.GetId()};
    chat.input = inputEntity.GetId();

    // Default singleplayer fallback when no SettingsMenu is present
    chat.Submit("Hello from singleplayer");
    chat.Refresh(Gameplay::HudContext{});
    CHECK(row.sender == "Player");
    CHECK(row.text == "Hello from singleplayer");
    CHECK_FALSE(row.developer);

    // With SettingsMenu configured
    auto& menuEntity = world.scene.CreateEntity("Settings");
    auto& settings = menuEntity.AddComponent<Gameplay::SettingsMenu>();
    settings.SetPlayerName("Tester");

    chat.Submit("Configured nickname message");
    chat.Refresh(Gameplay::HudContext{});
    CHECK(row.sender == "Tester");
    CHECK(row.text == "Configured nickname message");
    CHECK_FALSE(row.developer);

    // Submitting directly from UiInputField text (tests for dangling string_view bug)
    auto* field = inputEntity.GetComponent<Spot::UiInputField>();
    REQUIRE(field);
    field->SetText("A dynamically allocated text string in input field");
    chat.Submit(field->GetText());
    chat.Refresh(Gameplay::HudContext{});
    CHECK(row.text == "A dynamically allocated text string in input field");
    CHECK(field->GetText().empty());
}
