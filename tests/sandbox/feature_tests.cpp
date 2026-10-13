#include "physics_gun.h"
#include "player_controller.h"
#include "playground_fixture.h"
#include "prop_carrier.h"
#include "prop_spawner.h"
#include "tool_gun.h"
#include "undo_list.h"
#include "world_save.h"

#include <catch2/catch_approx.hpp>

TEST_CASE("The physics gun preserves the grabbed surface of scaled props and releases when disabled",
          "[sandbox][acceptance]") {
    SandboxTests::Playground game(true);
    game.Run(90);
    auto* player = game.scene.FindEntity("Player");
    REQUIRE(player);
    auto* control = player->GetComponent<PlayerController>();
    auto* gun = player->GetComponent<PhysicsGun>();
    REQUIRE(control);
    REQUIRE(gun);
    auto& prop = game.scene.CreateEntity("Scaled Prop");
    prop.GetTransform().SetScale({2, 1, 3});
    prop.GetTransform().SetWorldPosition(control->GetEyePosition() + control->GetAimDirection() * 4);
    prop.AddComponent<Spot::BoxCollider>();
    auto& body = prop.AddComponent<Spot::Rigidbody>();
    body.SetGravityScale(0);
    const auto origin = prop.GetTransform().GetWorldPosition();
    game.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
    game.Run(2);
    REQUIRE(gun->GetGrabbed() == prop.GetId());
    auto* hum = game.scene.GetEntity(gun->hum)->GetComponent<Spot::AudioSource>();
    REQUIRE(hum);
    CHECK(hum->IsPlaying());
    game.Run(15);
    CHECK(Spot::Length(prop.GetTransform().GetWorldPosition() - origin) < .025f);
    CHECK(Spot::Length(body.GetLinearVelocity()) < .025f);
    gun->SetEnabled(false);
    CHECK_FALSE(gun->IsGrabbing());
    CHECK_FALSE(hum->IsPlaying());
    game.Run(2);
    gun->SetEnabled(true);
    game.Run(2);
    CHECK_FALSE(gun->IsGrabbing());
}

TEST_CASE("Disabling a carrier releases its prop instead of resuming a stale hold", "[sandbox][acceptance]") {
    SandboxTests::Playground game;
    auto* player = game.scene.FindEntity("Player");
    auto* carrier = player->GetComponent<PropCarrier>();
    auto* control = player->GetComponent<PlayerController>();
    auto& prop = game.scene.CreateEntity("Carry Prop");
    prop.GetTransform().SetWorldPosition(control->GetEyePosition() + control->GetAimDirection() * 1.5f);
    prop.AddComponent<Spot::BoxCollider>();
    prop.AddComponent<Spot::Rigidbody>().SetGravityScale(0);
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::E, false});
    game.Step();
    REQUIRE(carrier->IsHolding());
    carrier->SetEnabled(false);
    CHECK_FALSE(carrier->IsHolding());
    carrier->SetEnabled(true);
    game.Step();
    CHECK_FALSE(carrier->IsHolding());
}

TEST_CASE("Spawn, freeze, checkpoint, pause and cleanup preserve the permanent level", "[sandbox][acceptance]") {
    SandboxTests::Playground game;
    auto* spawner = game.scene.FindComponent<PropSpawner>();
    auto* ui = game.scene.FindComponent<RuntimeInterface>();
    REQUIRE(spawner);
    REQUIRE(ui);
    const auto workshop = game.scene.FindEntity("Workshop")->GetId();
    const auto ground = game.scene.FindEntity("Ground")->GetId();
    ui->OpenSpawnMenu();
    REQUIRE(!ui->GetSpawnButtons().empty());
    game.scene.GetEntity(ui->GetSpawnButtons().front())->GetComponent<Spot::UiButton>()->Click();
    game.Run(2);
    REQUIRE(spawner->GetSpawnedCount() == 1);
    Spot::EntityId spawned = Spot::EntityId::Invalid;
    game.scene.ForEachComponent<Spot::Rigidbody>([&](Spot::Rigidbody& body) {
        if (spawner->IsSpawned(body.GetEntity().GetId())) {
            spawned = body.GetEntity().GetId();
            body.SetKinematic(true);
            body.GetEntity().AddTag(PhysicsGun::FrozenTag);
            body.GetEntity().SetName("Saved Construction");
        }
    });
    REQUIRE(spawned != Spot::EntityId::Invalid);
    ui->CloseMenus();
    const auto save = game.userData.GetPath() / "world.json";
    REQUIRE(WorldSave::Save(game.scene, save));
    ui->OpenPauseMenu();
    CHECK(Spot::Time::GetTimeScale() == 0);
    REQUIRE(WorldSave::Load(game.scene, save));
    auto* restored = game.scene.FindEntity("Saved Construction");
    REQUIRE(restored);
    CHECK(restored->HasTag(PhysicsGun::FrozenTag));
    CHECK(restored->GetComponent<Spot::Rigidbody>()->IsKinematic());
    CHECK(spawner->IsSpawned(restored->GetId()));
    CHECK(game.scene.GetEntity(workshop));
    CHECK(game.scene.GetEntity(ground));
    ui->CloseMenus();
    spawner->Cleanup();
    game.Run(2);
    CHECK(spawner->GetSpawnedCount() == 0);
    CHECK_FALSE(game.scene.FindEntity("Saved Construction"));
    CHECK(game.scene.GetEntity(workshop));
    CHECK(game.scene.FindEntitiesWithTag("SpawnArea").size() == 3);
    CHECK(game.scene.FindEntity("Workshop Ceiling Lamp"));
}
