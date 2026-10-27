#include "gameplay/core/players.h"
#include "playground_fixture.h"
#include "prop_spawner.h"
#include "undo_list.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/persistence/world_save.h"

#include <algorithm>

namespace WorldSave = Gameplay::WorldSave;

TEST_CASE("The generated target dummy can be spawned, shot, saved and undone", "[sandbox][target]") {
    SandboxTests::Playground game;
    game.Run(90);
    auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(game.scene);
    const auto catalog = spawner->GetSpawnables();
    const auto entry = std::ranges::find_if(catalog, [](const auto& item) { return item.name == "Target Dummy"; });
    REQUIRE(entry != catalog.end());
    auto* target = spawner->Spawn(static_cast<std::size_t>(entry - catalog.begin()));
    REQUIRE(target);
    target->SetName("Saved Target Dummy");
    REQUIRE(target->GetComponents<Spot::BoxCollider>().size() == 4);
    auto* player = Gameplay::Players::FindComponent<Gameplay::FirstPersonView>(game.scene);
    auto* body = target->GetComponent<Spot::Rigidbody>();
    REQUIRE(body);
    body->SetGravityScale(0);
    target->GetTransform().SetWorldPosition(player->GetEyePosition() + player->GetAimDirection() * 3 -
                                            Spot::Vector3{0, .19f, 0});
    game.Step();
    auto* holder = Gameplay::Players::FindComponent<Gameplay::Inventory>(game.scene);
    holder->Select(1);
    SandboxTests::Fire(*holder);
    CHECK(Spot::Dot(body->GetLinearVelocity(), player->GetAimDirection()) > .5f);
    const auto save = game.userData.GetPath() / "target.json";
    REQUIRE(WorldSave::Save(game.scene, save));
    REQUIRE(WorldSave::Load(game.scene, save));
    target = game.scene.FindEntity("Saved Target Dummy");
    REQUIRE(target);
    CHECK(spawner->IsSpawned(target->GetId()));
    CHECK(target->GetComponents<Spot::BoxCollider>().size() == 4);
    spawner->Cleanup();
    CHECK_FALSE(game.scene.FindEntity("Saved Target Dummy"));
    target = spawner->Spawn(static_cast<std::size_t>(entry - catalog.begin()));
    REQUIRE(target);
    const auto spawnedId = target->GetId();
    CHECK(Gameplay::Players::FindComponent<UndoList>(game.scene)->Undo());
    CHECK_FALSE(game.scene.GetEntity(spawnedId));
}
