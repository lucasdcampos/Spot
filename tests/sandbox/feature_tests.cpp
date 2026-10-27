#include "gameplay/core/players.h"
#include "grass_visibility.h"
#include "physics_gun.h"
#include "playground_fixture.h"
#include "prop_spawner.h"
#include "spawn_menu.h"
#include "tool_gun.h"
#include "undo_list.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/character/carrier.h"
#include "gameplay/persistence/world_save.h"

#include <catch2/catch_approx.hpp>

namespace WorldSave = Gameplay::WorldSave;

TEST_CASE("Grass chunks keep triangles crossing cell boundaries and their shading", "[sandbox]") {
    Spot::MeshData source;
    source.vertices = {{{-9, 0, -1}, {0, 1, 0}, {0, 0}, {1, 0, 0, -1}},
                       {{-7, 0, -1}, {0, 1, 0}, {1, 0}, {1, 0, 0, -1}},
                       {{-9, 1, 1}, {0, 1, 0}, {0, 1}, {1, 0, 0, -1}},
                       {{9, 0, 1}, {0, 1, 0}, {1, 1}, {1, 0, 0, -1}}};
    source.indices = {0, 1, 2, 1, 3, 2};
    const auto chunks = GrassChunks::Partition(source, 8.0f);
    REQUIRE(chunks.size() == 2);
    std::size_t index = 0;
    for (const auto& chunk : chunks) {
        REQUIRE(chunk.indices.size() == 3);
        for (const auto localIndex : chunk.indices) {
            REQUIRE(localIndex < chunk.vertices.size());
            const auto& actual = chunk.vertices[localIndex];
            const auto& expected = source.vertices[source.indices[index++]];
            CHECK(actual.position == expected.position);
            CHECK(actual.normal == expected.normal);
            CHECK(actual.uv == expected.uv);
            CHECK(actual.tangent == expected.tangent);
        }
    }
    CHECK(index == source.indices.size());
    CHECK(GrassChunks::Partition({}, 8.0f).empty());
    CHECK(GrassChunks::Partition(source, 0.0f).empty());
}

TEST_CASE("The physics gun preserves the grabbed surface of scaled props and releases when disabled",
          "[sandbox][acceptance]") {
    SandboxTests::Playground game(true);
    game.Run(90);
    auto* player = game.scene.FindEntity("Player");
    REQUIRE(player);
    auto* control = player->GetComponent<Gameplay::FirstPersonView>();
    auto* gun = player->GetComponentInChildren<PhysicsGun>();
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
    auto* carrier = player->GetComponent<Gameplay::Carrier>();
    auto* control = player->GetComponent<Gameplay::FirstPersonView>();
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
    auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(game.scene);
    auto* ui = game.scene.FindComponent<SpawnMenu>();
    REQUIRE(spawner);
    REQUIRE(ui);
    const auto workshop = game.scene.FindEntity("Workshop")->GetId();
    const auto ground = game.scene.FindEntity("Ground")->GetId();
    ui->Open();
    REQUIRE(!ui->GetSpawnButtons().empty());
    game.scene.GetEntity(ui->GetSpawnButtons().front())->GetComponent<Spot::UiButton>()->Click();
    REQUIRE(spawner->ConfirmPlacement());
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
    Gameplay::Menu::CloseAll(game.scene);
    const auto save = game.userData.GetPath() / "world.json";
    REQUIRE(WorldSave::Save(game.scene, save));
    game.scene.FindComponent<Gameplay::PauseMenu>()->Open();
    CHECK(Spot::Time::GetTimeScale() == 0);
    REQUIRE(WorldSave::Load(game.scene, save));
    auto* restored = game.scene.FindEntity("Saved Construction");
    REQUIRE(restored);
    CHECK(restored->HasTag(PhysicsGun::FrozenTag));
    CHECK(restored->GetComponent<Spot::Rigidbody>()->IsKinematic());
    CHECK(spawner->IsSpawned(restored->GetId()));
    CHECK(game.scene.GetEntity(workshop));
    CHECK(game.scene.GetEntity(ground));
    Gameplay::Menu::CloseAll(game.scene);
    spawner->Cleanup();
    game.Run(2);
    CHECK(spawner->GetSpawnedCount() == 0);
    CHECK_FALSE(game.scene.FindEntity("Saved Construction"));
    CHECK(game.scene.GetEntity(workshop));
    CHECK(game.scene.FindEntitiesWithTag("SpawnArea").size() == 3);
    CHECK(game.scene.FindEntity("Workshop Ceiling Lamp"));
}
