#include "../playground_fixture.h"

#include "developer_mode.h"
#include "gameplay/camera/first_person_view.h"
#include "gameplay/character/character_movement.h"
#include "gameplay/character/character_visual.h"
#include "gameplay/core/players.h"
#include "gameplay/effects/effect_pool.h"
#include "gameplay/health/health.h"
#include "gameplay/network/player_info.h"
#include "sandbox_commands.h"

#include <catch2/catch_approx.hpp>

namespace {

Gameplay::Firearm* FindRayGun(const Gameplay::Inventory& inventory) {
    for (auto* item : inventory.GetItems()) {
        if (item->displayName == "Ray Gun") {
            return dynamic_cast<Gameplay::Firearm*>(item);
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("The developer console easter egg adds only the developer skin and Ray Gun", "[sandbox][developer]") {
    SandboxTests::Playground game;
    auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(game.scene);
    auto* inventory = Gameplay::Players::FindComponent<Gameplay::Inventory>(game.scene);
    REQUIRE(mode);
    REQUIRE(inventory);
    CHECK_FALSE(mode->IsUnlocked());
    CHECK_FALSE(FindRayGun(*inventory));
    const int count = inventory->GetItemCount();
    Spot::Console console;
    SandboxCommands commands(console, game.scene);
    CHECK_FALSE(console.Execute("imthegamedev unexpected"));
    CHECK_FALSE(mode->IsUnlocked());
    REQUIRE(console.Execute("imthegamedev"));
    game.Run(2);
    REQUIRE(mode->IsUnlocked());
    CHECK(inventory->GetItemCount() == count + 1);
    auto* gun = FindRayGun(*inventory);
    REQUIRE(gun);
    CHECK(gun->damage == 1337.0f);
    CHECK(gun->infiniteAmmo);
    CHECK(gun->automatic);
    CHECK(gun->fireInterval == Catch::Approx(0.1f));
    CHECK(gun->GetAmmo() == gun->magazineSize);
    CHECK(gun->model.GetId().IsValid());
    CHECK(gun->icon.GetId().IsValid());
    CHECK(gun->fireSound.GetId().IsValid());
    REQUIRE(console.Execute("imthegamedev"));
    game.Run(2);
    CHECK(inventory->GetItemCount() == count + 1);
    inventory->Select(*gun);
    CHECK(inventory->GetEquipped() == gun);

    auto* view = Gameplay::Players::FindComponent<Gameplay::FirstPersonView>(game.scene);
    REQUIRE(view);
    auto& target = game.scene.CreateEntity("Ray Gun target");
    target.GetTransform().SetWorldPosition(view->GetEyePosition() + view->GetAimDirection() * 3.0f);
    target.AddComponent<Spot::BoxCollider>(Spot::Vector3{1, 1, 1});
    auto& health = target.AddComponent<Gameplay::Health>();
    health.maxHealth = 2000;
    game.Step();
    gun->Fire();
    CHECK(health.GetHealth() == Catch::Approx(663.0f));
    const int ammo = gun->GetAmmo();
    for (int shot = 0; shot < gun->magazineSize * 2; ++shot) {
        game.Run(8);
        REQUIRE(gun->TakeShot());
        CHECK_FALSE(gun->TakeShot());
    }
    CHECK(gun->GetAmmo() == gun->magazineSize);
    CHECK_FALSE(gun->StartReload());
    CHECK(gun->DescribeStatus().summary == "Infinite ammunition");
    REQUIRE(console.Execute("imthegamedev"));
    game.Step();
    CHECK(gun->GetAmmo() == ammo);
}

TEST_CASE("Only the exact developer nickname automatically unlocks its character", "[sandbox][developer]") {
    SandboxTests::Playground game;
    auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(game.scene);
    REQUIRE(mode);
    auto& info = game.scene.CreateEntity("Player Info").AddComponent<Gameplay::PlayerInfo>();
    info.pawn = mode->GetEntity().GetId();
    for (const auto* name : {"Player", "Lucasof", "lucasof2"}) {
        info.name = name;
        game.Step();
        CHECK_FALSE(mode->IsUnlocked());
    }
    info.name = "lucasof";
    game.Step();
    CHECK(mode->IsUnlocked());
    CHECK(info.developer);
    CHECK(FindRayGun(*mode->GetEntity().GetComponent<Gameplay::Inventory>()));
}

TEST_CASE("Developer commands smite and yeet only the named living player", "[sandbox][developer][powers]") {
    SandboxTests::Playground game;
    Spot::Console console;
    SandboxCommands commands(console, game.scene);
    auto* target = game.scene.Instantiate(game.assets.Load<Spot::Prefab>("prefabs/actors/player.prefab"));
    REQUIRE(target);
    target->GetComponent<Gameplay::Pawn>()->Teleport({0, 30, 0}, {}, {});
    auto& info = game.scene.CreateEntity("Target Info").AddComponent<Gameplay::PlayerInfo>();
    info.name = "Friend";
    info.pawn = target->GetId();
    game.Run(2);
    auto* health = target->GetComponent<Gameplay::Health>();
    REQUIRE(health);
    CHECK_FALSE(console.Execute("smite Friend"));
    CHECK_FALSE(console.Execute("yeet Friend"));
    CHECK_FALSE(health->IsDead());
    REQUIRE(console.Execute("imthegamedev"));
    CHECK_FALSE(console.Execute("smite"));
    CHECK_FALSE(console.Execute("yeet Friend extra"));
    CHECK_FALSE(console.Execute("smite Nobody"));
    CHECK_FALSE(console.Execute("smite \"Friend Name\""));
    const auto before = target->GetTransform().GetWorldPosition();
    REQUIRE(console.Execute("yeet Friend"));
    game.Run(10);
    const auto after = target->GetTransform().GetWorldPosition();
    CHECK(after.y > before.y + 2);
    CHECK(Spot::Length(Spot::Vector3{after.x - before.x, 0, after.z - before.z}) > 4);
    CHECK_FALSE(health->IsDead());
    auto& duplicate = game.scene.CreateEntity("Duplicate Info").AddComponent<Gameplay::PlayerInfo>();
    duplicate.name = info.name;
    duplicate.pawn = Gameplay::Players::GetBody(game.scene)->GetEntity().GetId();
    CHECK_FALSE(console.Execute("smite Friend"));
    duplicate.name = "Other";
    REQUIRE(console.Execute("smite Friend"));
    CHECK(health->IsDead());
    CHECK_FALSE(Gameplay::Players::FindComponent<Gameplay::Health>(game.scene)->IsDead());
    CHECK(Gameplay::EffectPool::Get(game.scene).GetEmitterCount() >= 9);
    CHECK_FALSE(console.Execute("smite Friend"));
    CHECK_FALSE(console.Execute("yeet Friend"));
}
