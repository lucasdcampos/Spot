#include "../playground_fixture.h"
#include "temp_directory.h"

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

#include <fstream>
#include <string>

namespace {

Gameplay::Firearm* FindRayGun(const Gameplay::Inventory& inventory) {
    for (auto* item : inventory.GetItems()) {
        if (item->displayName == "Ray Gun") {
            return dynamic_cast<Gameplay::Firearm*>(item);
        }
    }
    return nullptr;
}

std::string HexEncode(std::span<const std::uint8_t> bytes) {
    static constexpr char Digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(Digits[b >> 4]);
        out.push_back(Digits[b & 0x0f]);
    }
    return out;
}

} // namespace

TEST_CASE("The developer console easter egg requires cheats and adds only the developer skin and Ray Gun",
          "[sandbox][developer]") {
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
    CHECK_FALSE(console.Execute("imthegamedev"));
    CHECK_FALSE(mode->IsUnlocked());
    console.SetCheatsEnabled(true);
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

TEST_CASE("Only a matching Ed25519 private key in developer.key automatically unlocks the character",
          "[sandbox][developer]") {
    // RFC 8032 test vector 1.
    constexpr DeveloperMode::Seed RfcSeed = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                             0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                             0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
    constexpr DeveloperMode::PublicKey RfcPublic = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
                                                    0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
                                                    0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
    CHECK(DeveloperMode::DerivePublicKey(RfcSeed) == RfcPublic);
    const std::array<std::uint8_t, 4> message = {0x72, 0xaf, 0x01, 0x99};
    const auto signature = DeveloperMode::Sign(RfcSeed, message);
    CHECK(DeveloperMode::Verify(RfcPublic, message, signature));
    CHECK_FALSE(DeveloperMode::Verify(DeveloperMode::DefaultPublicKey, message, signature));
    auto tampered = signature;
    tampered[0] ^= 0x01;
    CHECK_FALSE(DeveloperMode::Verify(RfcPublic, message, tampered));

    SandboxTests::Playground game;
    auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(game.scene);
    REQUIRE(mode);
    auto& info = game.scene.CreateEntity("Player Info").AddComponent<Gameplay::PlayerInfo>();
    info.pawn = mode->GetEntity().GetId();
    for (const auto* name : {"Player", "Lucasof", "lucasof"}) {
        info.name = name;
        game.Step();
        CHECK_FALSE(mode->IsUnlocked());
    }

    TempDirectory directory;
    const auto keyFile = directory.GetPath() / DeveloperMode::KeyFileName;
    mode->SetPublicKey(RfcPublic);

    // Wrong private key is rejected.
    DeveloperMode::Seed wrongSeed = RfcSeed;
    wrongSeed[0] ^= 0xff;
    std::ofstream(keyFile) << HexEncode(wrongSeed) << '\n';
    mode->SetKeyPath(keyFile);
    game.Step();
    CHECK_FALSE(mode->IsUnlocked());

    // Matching private key unlocks the skin and Ray Gun.
    std::ofstream(keyFile, std::ios::trunc) << HexEncode(RfcSeed) << '\n';
    mode->SetKeyPath(keyFile);
    game.Step();
    CHECK(mode->IsUnlocked());
    CHECK(info.developer);
    CHECK(FindRayGun(*mode->GetEntity().GetComponent<Gameplay::Inventory>()));

    if (const auto realKey = DeveloperMode::GetDefaultKeyPath(); std::filesystem::is_regular_file(realKey)) {
        SandboxTests::Playground localDev;
        auto* localMode = Gameplay::Players::FindComponent<DeveloperMode>(localDev.scene);
        REQUIRE(localMode);
        localMode->SetKeyPath(realKey);
        localDev.Step();
        CHECK(localMode->IsUnlocked());
    }
}

TEST_CASE("Developer commands smite and yeet require cheats and strike only the named living player",
          "[sandbox][developer][powers]") {
    SandboxTests::Playground game(true);
    REQUIRE(game.audio);
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
    // Even with developer mode unlocked (as from developer.key), smite and yeet require cheats or operator permission.
    auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(game.scene);
    REQUIRE(mode);
    CHECK(mode->smiteSound.GetId().IsValid());
    mode->Unlock();
    REQUIRE(mode->IsUnlocked());
    CHECK_FALSE(console.Execute("smite Friend"));
    CHECK_FALSE(console.Execute("yeet Friend"));
    CHECK_FALSE(health->IsDead());

    console.SetCheatsEnabled(true);
    CHECK_FALSE(console.Execute("smite"));
    CHECK_FALSE(console.Execute("yeet Friend extra"));
    CHECK_FALSE(console.Execute("smite Nobody"));
    CHECK_FALSE(console.Execute("smite \"Friend Name\""));
    const auto before = target->GetTransform().GetWorldPosition();
    REQUIRE(console.Execute("yeet Friend"));
    game.Run(60);
    const auto after = target->GetTransform().GetWorldPosition();
    CHECK(after.y > before.y + 80);
    CHECK(Spot::Length(Spot::Vector3{after.x - before.x, 0, after.z - before.z}) > 130);
    CHECK_FALSE(health->IsDead());
    auto& duplicate = game.scene.CreateEntity("Duplicate Info").AddComponent<Gameplay::PlayerInfo>();
    duplicate.name = info.name;
    duplicate.pawn = Gameplay::Players::GetBody(game.scene)->GetEntity().GetId();
    CHECK_FALSE(console.Execute("smite Friend"));
    duplicate.name = "Other";
    target->GetComponent<Gameplay::Pawn>()->Teleport({0, 30, 0}, {}, {});
    game.Run(2);
    std::vector<float> mix(48000 / 60 * 2);
    for (int i = 0; i < 4; ++i) {
        game.audio->Render(mix);
    }
    REQUIRE(console.Execute("smite Friend"));
    game.Step();
    game.audio->Render(mix);
    const auto effectsBus = game.audio->FindBus("Effects");
    REQUIRE(effectsBus);
    CHECK(game.audio->GetBusPeak(*effectsBus).left > 0.05f);
    CHECK(health->IsDead());
    CHECK_FALSE(Gameplay::Players::FindComponent<Gameplay::Health>(game.scene)->IsDead());
    CHECK(Gameplay::EffectPool::Get(game.scene).GetEmitterCount() >= 9);
    CHECK_FALSE(console.Execute("smite Friend"));
    CHECK_FALSE(console.Execute("yeet Friend"));
}
