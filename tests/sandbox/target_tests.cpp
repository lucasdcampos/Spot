#include "player_controller.h"
#include "playground_fixture.h"
#include "prop_spawner.h"
#include "undo_list.h"
#include "weapon_holder.h"
#include "world_save.h"

#include <algorithm>

TEST_CASE("The generated target dummy can be spawned, shot, saved and undone", "[sandbox][target]") {
    SandboxTests::Playground game;
    game.Run(90);
    auto* spawner = game.scene.FindComponent<PropSpawner>();
    const auto catalog = spawner->GetSpawnables();
    const auto entry = std::ranges::find_if(catalog, [](const auto& item) { return item.name == "Target Dummy"; });
    REQUIRE(entry != catalog.end());
    auto* target = spawner->Spawn(static_cast<std::size_t>(entry - catalog.begin()));
    REQUIRE(target);
    target->SetName("Saved Target Dummy");
    REQUIRE(target->GetComponents<Spot::BoxCollider>().size() == 4);
    auto* player = game.scene.FindComponent<PlayerController>();
    auto* body = target->GetComponent<Spot::Rigidbody>();
    REQUIRE(body);
    body->SetGravityScale(0);
    target->GetTransform().SetWorldPosition(player->GetEyePosition() + player->GetAimDirection() * 3 -
                                            Spot::Vector3{0, .19f, 0});
    game.Step();
    auto* holder = game.scene.FindComponent<WeaponHolder>();
    holder->Select(1);
    holder->Fire();
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
    CHECK(game.scene.FindComponent<UndoList>()->Undo());
    CHECK_FALSE(game.scene.GetEntity(spawnedId));
}
