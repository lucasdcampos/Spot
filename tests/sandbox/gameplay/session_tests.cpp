#include "gameplay_world.h"
#include "temp_directory.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/players.h"
#include "gameplay/game/death_screen_element.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/game/session_controls.h"
#include "gameplay/game/spawn_point.h"
#include "gameplay/health/health.h"
#include "gameplay/hud/hud.h"
#include "gameplay/menus/pause_menu.h"
#include "gameplay/persistence/world_save.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

struct SessionWorld : GameplayTests::World {
    Gameplay::GameMode& mode = scene.CreateEntity("Rules").AddComponent<Gameplay::GameMode>();

    Gameplay::PlayerController& AddPlayer(Spot::Vector3 position) {
        auto& entity = scene.CreateEntity("Pawn");
        entity.GetTransform().SetPosition(position);
        entity.AddComponent<Gameplay::Pawn>();
        entity.AddComponent<Gameplay::Health>();
        auto& controller = scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>();
        controller.pawn = entity.GetId();
        return controller;
    }

    void Tap(Spot::Key key) {
        input.OnEvent(Spot::KeyPressedEvent{key, false});
        Step();
        input.OnEvent(Spot::KeyReleasedEvent{key});
        Step();
    }
};

} // namespace

TEST_CASE("Game mode respawns each local player at its own start", "[sandbox][gameplay][session]") {
    SessionWorld world;
    world.mode.respawnTime = 0.5f;
    auto& first = world.AddPlayer({1.0f, 2.0f, 3.0f});
    auto& second = world.AddPlayer({4.0f, 2.0f, 3.0f});
    world.Step();
    auto* pawn = first.GetPawn();
    REQUIRE(pawn);
    auto& health = *pawn->GetEntity().GetComponent<Gameplay::Health>();
    pawn->GetEntity().GetTransform().SetPosition({20.0f, 2.0f, 3.0f});
    health.TakeDamage({.amount = 1000.0f});
    CHECK(world.mode.GetRespawnTimeLeft(first) == Catch::Approx(0.5f));
    CHECK(world.mode.GetRespawnTimeLeft(second) == 0.0f);
    world.Run(0.25f);
    CHECK(health.IsDead());
    // The scene supplies scaled time, so a paused frame does not advance the respawn timer.
    const float left = world.mode.GetRespawnTimeLeft(first);
    world.Step(0.0f);
    CHECK(world.mode.GetRespawnTimeLeft(first) == left);
    world.Run(0.35f);
    CHECK_FALSE(health.IsDead());
    CHECK(health.GetHealth() == health.maxHealth);
    CHECK(first.GetPawn() == pawn);
    CHECK(pawn->GetEntity().GetTransform().GetWorldPosition() == Spot::Vector3{1.0f, 2.0f, 3.0f});
    CHECK(second.GetPawn()->GetEntity().GetTransform().GetWorldPosition() == Spot::Vector3{4.0f, 2.0f, 3.0f});
}

TEST_CASE("Game mode returns a fallen pawn without healing it or affecting seated pawns",
          "[sandbox][gameplay][session]") {
    SessionWorld world;
    auto& player = world.AddPlayer({2.0f, 1.0f, 3.0f});
    world.Step();
    auto* pawn = player.GetPawn();
    REQUIRE(pawn);
    auto& health = *pawn->GetEntity().GetComponent<Gameplay::Health>();
    health.TakeDamage({.amount = 20.0f});
    pawn->GetEntity().GetTransform().SetPosition({0.0f, world.mode.fallHeight - 1.0f, 0.0f});
    pawn->SetState("Seated", true);
    world.Step();
    CHECK(pawn->GetEntity().GetTransform().GetWorldPosition().y < world.mode.fallHeight);
    pawn->SetState("Seated", false);
    world.Step();
    CHECK(pawn->GetEntity().GetTransform().GetWorldPosition() == Spot::Vector3{2.0f, 1.0f, 3.0f});
    CHECK(health.GetHealth() == Catch::Approx(80.0f));
}

TEST_CASE("Game mode tracks late players and cancels work for destroyed players", "[sandbox][gameplay][session]") {
    SessionWorld world;
    world.Step();
    auto& player = world.AddPlayer({3.0f, 1.0f, 0.0f});
    world.Step();
    REQUIRE(player.GetPawn());
    player.GetPawn()->GetEntity().GetComponent<Gameplay::Health>()->TakeDamage({.amount = 1000.0f});
    player.GetEntity().Destroy();
    world.Run(world.mode.respawnTime + 0.1f);
    CHECK(Gameplay::Players::GetAll(world.scene).empty());
}

TEST_CASE("Player component lookup follows the occupant and respects the local player index",
          "[sandbox][gameplay][session]") {
    SessionWorld world;
    auto& first = world.AddPlayer({});
    auto& second = world.AddPlayer({});
    world.Step();
    auto* body = first.GetPawn();
    auto& vehicle = world.scene.CreateEntity("Other pawn").AddComponent<Gameplay::Pawn>();
    vehicle.SetOccupant(body);
    first.Possess(vehicle);
    CHECK(Gameplay::Players::GetPawn(world.scene) == &vehicle);
    CHECK(Gameplay::Players::GetBody(world.scene) == body);
    CHECK(Gameplay::Players::FindComponent<Gameplay::Health>(world.scene) ==
          body->GetEntity().GetComponent<Gameplay::Health>());
    CHECK(Gameplay::Players::GetBody(world.scene, 1) == second.GetPawn());
    CHECK(Gameplay::Players::FindComponent<Gameplay::Health>(world.scene, 2) == nullptr);
}

TEST_CASE("The death screen reads the respawn timer from the game mode", "[sandbox][gameplay][session]") {
    SessionWorld world;
    world.mode.respawnTime = 2.0f;
    auto& player = world.AddPlayer({});
    auto& hud = world.scene.CreateEntity("HUD");
    hud.AddComponent<Gameplay::Hud>();
    auto& panel = hud.CreateChild("Death");
    auto& death = panel.AddComponent<Gameplay::DeathScreenElement>();
    auto& label = panel.CreateChild("Timer").AddComponent<Spot::UiText>();
    death.timer = label.GetEntity().GetId();
    world.Step();
    player.GetPawn()->GetEntity().GetComponent<Gameplay::Health>()->TakeDamage({.amount = 1000.0f});
    world.Step();
    CHECK(death.IsShown());
    CHECK(label.text == "Respawning in 2");
    world.Run(2.1f);
    CHECK_FALSE(death.IsShown());
}

TEST_CASE("Session shortcuts use configured actions and keep menu clicks out of cursor capture",
          "[sandbox][gameplay][session]") {
    SessionWorld world;
    auto& session = world.scene.CreateEntity("Session").AddComponent<Gameplay::SessionControls>();
    session.pauseAction = "Release cursor";
    session.captureAction = "Capture cursor";
    world.actions.Add(session.pauseAction, Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::P));
    world.actions.Add(session.captureAction, Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::F));
    world.Step();
    CHECK(Spot::Input::IsCursorLocked());
    world.Tap(Spot::Key::P);
    CHECK_FALSE(Spot::Input::IsCursorLocked());
    world.Tap(Spot::Key::F);
    CHECK(Spot::Input::IsCursorLocked());

    auto& menu = world.scene.CreateEntity("Menu").AddComponent<Gameplay::Menu>();
    world.Step();
    menu.Open();
    Spot::Input::SetCursorLocked(false);
    world.Tap(Spot::Key::F);
    CHECK_FALSE(Spot::Input::IsCursorLocked());
    menu.Close();
}

TEST_CASE("Session quick save and load work without sandbox components", "[sandbox][gameplay][session]") {
    SessionWorld world;
    TempDirectory directory;
    auto& session = world.scene.CreateEntity("Session").AddComponent<Gameplay::SessionControls>();
    session.SetSaveDirectory(directory.GetPath());
    session.checkpoint = "checkpoint.json";
    session.saveAction = "Save";
    session.loadAction = "Load";
    world.actions.Add(session.saveAction, Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::F9));
    world.actions.Add(session.loadAction, Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::F10));
    auto& prop = world.scene.CreateEntity("Saved prop");
    prop.AddComponent<Gameplay::Persistent>();
    prop.GetTransform().SetPosition({1.0f, 2.0f, 3.0f});
    world.Step();
    world.Tap(Spot::Key::F9);
    CHECK(std::filesystem::is_regular_file(directory.GetPath() / "checkpoint.json"));
    prop.Destroy();
    world.Step();
    world.Tap(Spot::Key::F10);
    const auto* restored = world.scene.FindEntity("Saved prop");
    REQUIRE(restored);
    CHECK(restored->GetTransform().GetWorldPosition() == Spot::Vector3{1.0f, 2.0f, 3.0f});
}

TEST_CASE("Disabling game mode cancels respawn and keeps starting positions on reenable",
          "[sandbox][gameplay][session]") {
    SessionWorld world;
    world.mode.respawnTime = 0.2f;
    auto& player = world.AddPlayer({2.0f, 1.0f, 3.0f});
    world.Step();
    auto* pawn = player.GetPawn();
    auto& health = *pawn->GetEntity().GetComponent<Gameplay::Health>();
    pawn->GetEntity().GetTransform().SetPosition({10.0f, 1.0f, 3.0f});
    health.TakeDamage({.amount = 1000.0f});
    world.mode.SetEnabled(false);
    world.Run(0.5f);
    CHECK(health.IsDead());
    world.mode.SetEnabled(true);
    world.Run(0.3f);
    CHECK_FALSE(health.IsDead());
    CHECK(pawn->GetEntity().GetTransform().GetWorldPosition() == Spot::Vector3{2.0f, 1.0f, 3.0f});
}

TEST_CASE("Game mode makes players' pawns at spawn points and respawns them away from others",
          "[sandbox][gameplay][session]") {
    SessionWorld world;
    world.mode.respawnTime = 0.1f;
    Spot::Scene authoring;
    Spot::Entity& body = authoring.CreateEntity("Body");
    body.AddComponent<Gameplay::Pawn>();
    body.AddComponent<Gameplay::Health>();
    auto prefab = Spot::Prefab::FromString(body.SaveToString());
    REQUIRE(prefab);
    world.mode.playerPrefab =
        Spot::AssetRef<Spot::Prefab>(Spot::AssetId{7, 7}, std::make_shared<const Spot::Prefab>(std::move(*prefab)));

    const auto addSpawn = [&](Spot::Vector3 position, std::string tag = {}) {
        auto& entity = world.scene.CreateEntity("Spawn");
        entity.GetTransform().SetPosition(position);
        entity.GetTransform().SetRotation(Spot::Quaternion::FromEuler({0.0f, 1.0f, 0.0f}));
        entity.AddComponent<Gameplay::SpawnPoint>().tag = std::move(tag);
    };
    addSpawn({0.0f, 1.0f, 0.0f});
    addSpawn({30.0f, 1.0f, 0.0f});
    addSpawn({-30.0f, 1.0f, 0.0f}, "Other mode");
    world.mode.spawnTag = "Free";

    auto& first = world.scene.CreateEntity("First").AddComponent<Gameplay::PlayerController>();
    world.Step();
    REQUIRE(first.GetPawn());
    Spot::Entity& firstBody = first.GetPawn()->GetEntity();
    // Alone, the first spawn point, facing as it does.
    CHECK(firstBody.GetTransform().GetWorldPosition() == Spot::Vector3{0.0f, 1.0f, 0.0f});
    CHECK(firstBody.GetTransform().GetWorldRotation() == Spot::Quaternion::FromEuler({0.0f, 1.0f, 0.0f}));
    CHECK(firstBody.GetPrefabAssetId() == Spot::AssetId{7, 7});

    auto& second = world.scene.CreateEntity("Second").AddComponent<Gameplay::PlayerController>();
    world.Step();
    REQUIRE(second.GetPawn());
    // The farthest from the first player, of the mode's spawn points.
    CHECK(second.GetPawn()->GetEntity().GetTransform().GetWorldPosition() == Spot::Vector3{30.0f, 1.0f, 0.0f});

    // The second player stands on the far spawn point, so the first dies and comes back at the near one.
    firstBody.GetTransform().SetPosition({25.0f, 1.0f, 0.0f});
    firstBody.GetComponent<Gameplay::Health>()->TakeDamage({.amount = 1000.0f});
    world.Run(0.2f);
    CHECK_FALSE(firstBody.GetComponent<Gameplay::Health>()->IsDead());
    CHECK(firstBody.GetTransform().GetWorldPosition() == Spot::Vector3{0.0f, 1.0f, 0.0f});
}
