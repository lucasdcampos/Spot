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
#include "sandbox_hud.h"

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
    CHECK_FALSE(mode->IsAuthenticated());
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
    CHECK_FALSE(mode->IsAuthenticated());
    CHECK_FALSE(info.developerAuthenticated);

    // A prior cheat unlock must not prevent later key authentication.
    mode->Unlock();
    CHECK_FALSE(mode->IsAuthenticated());

    // Matching private key unlocks the skin and Ray Gun.
    std::ofstream(keyFile, std::ios::trunc) << HexEncode(RfcSeed) << '\n';
    mode->SetKeyPath(keyFile);
    game.Step();
    CHECK(mode->IsUnlocked());
    CHECK(info.developer);
    CHECK(mode->IsAuthenticated());
    CHECK(info.developerAuthenticated);
    CHECK(FindRayGun(*mode->GetEntity().GetComponent<Gameplay::Inventory>()));

    auto& replacement = game.scene.CreateEntity("Replacement character").AddComponent<DeveloperMode>();
    info.pawn = replacement.GetEntity().GetId();
    game.Step();
    CHECK(replacement.IsAuthenticated());
    CHECK(replacement.IsUnlocked());

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

TEST_CASE("God mode requires cheats and blocks damage without granting developer mode", "[sandbox][god]") {
    SandboxTests::Playground game;
    Spot::Console console;
    SandboxCommands commands(console, game.scene);
    auto* health = Gameplay::Players::FindComponent<Gameplay::Health>(game.scene);
    auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(game.scene);
    REQUIRE(health);
    REQUIRE(mode);
    CHECK_FALSE(console.Execute("god"));
    CHECK_FALSE(health->IsGod());
    console.SetCheatsEnabled(true);
    CHECK_FALSE(console.Execute("god invalid"));
    CHECK_FALSE(console.Execute("god true extra"));
    REQUIRE(console.Execute("god"));
    CHECK(health->IsGod());
    const float before = health->GetHealth();
    CHECK(health->TakeDamage({.amount = 1000, .type = "Fall"}) == 0);
    CHECK(health->GetHealth() == before);
    CHECK_FALSE(mode->IsUnlocked());
    auto& attacker = game.scene.CreateEntity("Attacker");
    attacker.AddComponent<Gameplay::Pawn>();
    auto& info = game.scene.CreateEntity("Attacker info").AddComponent<Gameplay::PlayerInfo>();
    info.pawn = attacker.GetId();
    health->TakeDamage({.amount = 10, .instigator = attacker.GetId()});
    CHECK(mode->GetImmortalTime() == 0);
    mode->Unlock();
    health->TakeDamage({.amount = 10});
    CHECK(mode->GetImmortalTime() == 0);
    health->TakeDamage({.amount = 10, .instigator = health->GetEntity().GetId()});
    CHECK(mode->GetImmortalTime() == 0);
    health->TakeDamage({.amount = 10, .instigator = attacker.GetId()});
    CHECK(mode->GetImmortalTime() == Catch::Approx(3));
    game.Step(1);
    CHECK(mode->GetImmortalTime() == Catch::Approx(2));
    health->TakeDamage({.amount = 10, .instigator = attacker.GetId()});
    CHECK(mode->GetImmortalTime() == Catch::Approx(3));
    game.Step(3.1f);
    CHECK(mode->GetImmortalTime() == 0);
    REQUIRE(console.Execute("god false"));
    CHECK_FALSE(health->IsGod());
    CHECK(health->TakeDamage({.amount = 10}) > 0);
    REQUIRE(console.Execute("god true"));
    REQUIRE(console.Execute("god"));
    CHECK_FALSE(health->IsGod());
}

TEST_CASE("Player labels project nearby nicknames and hide distant occluded and dead players", "[sandbox][labels]") {
    SandboxTests::Playground game;
    auto* camera = Spot::FindMainCamera(game.scene);
    REQUIRE(camera);
    const auto view = camera->GetCamera3D();
    auto& target = game.scene.CreateEntity("Named player");
    auto& pawn = target.AddComponent<Gameplay::Pawn>();
    target.GetTransform().SetWorldPosition(view.position + view.GetForward() * 5.0f);
    auto& info = game.scene.CreateEntity("Named info").AddComponent<Gameplay::PlayerInfo>();
    info.name = "Nearby";
    info.pawn = target.GetId();
    game.Step();
    const Spot::Vector2 size{1024, 768};
    auto labels = GetPlayerLabels(game.scene, size);
    REQUIRE(labels.size() == 1);
    CHECK(labels[0].name == "Nearby");
    CHECK_FALSE(labels[0].developerAuthenticated);
    CHECK(labels[0].position.x == Catch::Approx(512).margin(2));
    CHECK(labels[0].position.y > 384);
    pawn.SetState("Dead", true);
    CHECK(GetPlayerLabels(game.scene, size).empty());
    pawn.SetState("Dead", false);
    target.GetTransform().SetWorldPosition(view.position + view.GetForward() * 25.0f);
    CHECK(GetPlayerLabels(game.scene, size).empty());
    target.GetTransform().SetWorldPosition(view.position - view.GetForward() * 5.0f);
    CHECK(GetPlayerLabels(game.scene, size).empty());
    target.GetTransform().SetWorldPosition(view.position + view.GetForward() * 5.0f);
    auto& wall = game.scene.CreateEntity("Occluder");
    wall.GetTransform().SetWorldPosition(view.position + view.GetForward() * 2.5f);
    wall.AddComponent<Spot::BoxCollider>(Spot::Vector3{2, 3, 0.3f});
    game.Run(2);
    CHECK(GetPlayerLabels(game.scene, size).empty());
}

TEST_CASE("Backrooms command builds a shared yellow maze on demand with per-player timers and death cleanup",
          "[sandbox][developer][backrooms]") {
    SandboxTests::Playground game(true);
    REQUIRE(game.audio);
    const auto run = [&](int frames) {
        std::array<float, 1600> samples{};
        for (int frame = 0; frame < frames; ++frame) {
            game.Step();
            game.audio->Render(samples);
        }
    };
    Spot::Console console;
    SandboxCommands commands(console, game.scene);
    auto* musicEntity = game.scene.FindEntity("Music");
    REQUIRE(musicEntity);
    auto* musicSource = musicEntity->GetComponent<Spot::AudioSource>();
    REQUIRE(musicSource);
    auto* localBody = Gameplay::Players::GetBody(game.scene);
    REQUIRE(localBody);
    auto& localEntity = localBody->GetEntity();
    auto* localPawn = localEntity.GetComponent<Gameplay::Pawn>();
    REQUIRE(localPawn);
    localPawn->Teleport({-8.0f, 1.0f, 6.0f}, {}, {0.0f, 0.0f, -1.0f});

    auto* friendEntity = game.scene.Instantiate(game.assets.Load<Spot::Prefab>("prefabs/actors/player.prefab"));
    REQUIRE(friendEntity);
    auto* friendPawn = friendEntity->GetComponent<Gameplay::Pawn>();
    REQUIRE(friendPawn);
    friendPawn->Teleport({12.0f, 1.0f, -4.0f}, {}, {1.0f, 0.0f, 0.0f});
    auto& friendInfo = game.scene.CreateEntity("Friend Info").AddComponent<Gameplay::PlayerInfo>();
    friendInfo.name = "Friend";
    friendInfo.pawn = friendEntity->GetId();
    run(15);
    REQUIRE(musicSource->IsPlaying());

    const auto localBefore = localEntity.GetTransform().GetWorldPosition();
    const auto friendBefore = friendEntity->GetTransform().GetWorldPosition();
    CHECK_FALSE(DeveloperMode::IsBackroomsActive(game.scene));

    // Requires cheats and valid arguments.
    CHECK_FALSE(console.Execute("backrooms Friend 10"));
    console.SetCheatsEnabled(true);
    CHECK_FALSE(console.Execute("backrooms"));
    CHECK_FALSE(console.Execute("backrooms Friend"));
    CHECK_FALSE(console.Execute("backrooms Friend -5"));
    CHECK_FALSE(console.Execute("backrooms Friend nope"));
    CHECK_FALSE(console.Execute("backrooms Nobody 10"));
    CHECK_FALSE(DeveloperMode::IsBackroomsActive(game.scene));

    // Send Player for 60s and Friend for 2s into the same Backrooms instance.
    REQUIRE(console.Execute("backrooms Player 60"));
    REQUIRE(DeveloperMode::IsBackroomsActive(game.scene));
    CHECK_FALSE(musicSource->IsPlaying());
    auto* backrooms = game.scene.FindEntity("Backrooms");
    REQUIRE(backrooms);
    std::size_t ceilingLights = 0;
    std::size_t ambientLights = 0;
    for (auto* child : backrooms->GetChildren()) {
        if (const auto* light = child->GetComponent<Spot::PointLight>()) {
            if (child->GetName() == "Backrooms Light") {
                CHECK(light->GetColor() == Spot::Color::White);
                ++ceilingLights;
            } else if (child->GetName() == "Backrooms Ambient") {
                CHECK(light->GetColor().r > light->GetColor().b);
                ++ambientLights;
            }
        }
    }
    CHECK(ceilingLights >= 9);
    CHECK(ambientLights >= 9);

    REQUIRE(console.Execute("backrooms Friend 2"));
    CHECK(game.scene.FindEntity("Backrooms") == backrooms);
    CHECK(Spot::Length(localEntity.GetTransform().GetWorldPosition() - DeveloperMode::BackroomsOrigin) < 20.0f);
    CHECK(Spot::Length(friendEntity->GetTransform().GetWorldPosition() - DeveloperMode::BackroomsOrigin) < 20.0f);
    REQUIRE(DeveloperMode::GetBackroomsTimeLeft(localEntity).has_value());
    REQUIRE(DeveloperMode::GetBackroomsTimeLeft(*friendEntity).has_value());
    CHECK(*DeveloperMode::GetBackroomsTimeLeft(localEntity) > 50.0f);
    CHECK(*DeveloperMode::GetBackroomsTimeLeft(*friendEntity) <= 2.0f);

    // Advance 2.5 seconds: Friend's 2s timer expires and returns Friend to their last position; Player remains inside.
    run(150);
    CHECK_FALSE(DeveloperMode::GetBackroomsTimeLeft(*friendEntity).has_value());
    CHECK(Spot::Length(friendEntity->GetTransform().GetWorldPosition() - friendBefore) < 0.5f);
    REQUIRE(DeveloperMode::GetBackroomsTimeLeft(localEntity).has_value());
    CHECK(*DeveloperMode::GetBackroomsTimeLeft(localEntity) > 55.0f);
    CHECK(Spot::Length(localEntity.GetTransform().GetWorldPosition() - DeveloperMode::BackroomsOrigin) < 20.0f);
    CHECK(DeveloperMode::IsBackroomsActive(game.scene));
    CHECK_FALSE(musicSource->IsPlaying());

    // Duration 0 keeps Friend in the Backrooms until death; updating Player's timer to 1s preserves Player's return
    // spot.
    REQUIRE(console.Execute("backrooms Friend 0"));
    REQUIRE(DeveloperMode::GetBackroomsTimeLeft(*friendEntity).has_value());
    CHECK(*DeveloperMode::GetBackroomsTimeLeft(*friendEntity) == 0.0f);
    REQUIRE(console.Execute("backrooms Player 1"));
    run(90);
    CHECK_FALSE(DeveloperMode::GetBackroomsTimeLeft(localEntity).has_value());
    CHECK(Spot::Length(localEntity.GetTransform().GetWorldPosition() - localBefore) < 0.5f);
    CHECK(musicSource->IsPlaying());
    // Friend is still inside with duration 0 after 1.5s, so the Backrooms stay alive.
    REQUIRE(DeveloperMode::GetBackroomsTimeLeft(*friendEntity).has_value());
    CHECK(DeveloperMode::IsBackroomsActive(game.scene));

    // When Friend dies in the Backrooms, their tracking is removed and the empty Backrooms disappear immediately.
    REQUIRE(console.Execute("smite Friend"));
    run(1);
    CHECK(friendEntity->GetComponent<Gameplay::Health>()->IsDead());
    CHECK_FALSE(DeveloperMode::GetBackroomsTimeLeft(*friendEntity).has_value());
    CHECK_FALSE(DeveloperMode::IsBackroomsActive(game.scene));

    // Respawn Friend and verify "backrooms all <duration>" sends every living player and cleans up when they return.
    friendEntity->GetComponent<Gameplay::Health>()->Restore();
    friendPawn->Teleport(friendBefore, {}, {1.0f, 0.0f, 0.0f});
    run(2);
    REQUIRE(console.Execute("backrooms all 1"));
    CHECK(DeveloperMode::IsBackroomsActive(game.scene));
    CHECK(DeveloperMode::GetBackroomsTimeLeft(localEntity).has_value());
    CHECK(DeveloperMode::GetBackroomsTimeLeft(*friendEntity).has_value());
    run(90);
    CHECK_FALSE(DeveloperMode::GetBackroomsTimeLeft(localEntity).has_value());
    CHECK_FALSE(DeveloperMode::GetBackroomsTimeLeft(*friendEntity).has_value());
    CHECK_FALSE(DeveloperMode::IsBackroomsActive(game.scene));
    CHECK(Spot::Length(localEntity.GetTransform().GetWorldPosition() - localBefore) < 0.5f);
    CHECK(Spot::Length(friendEntity->GetTransform().GetWorldPosition() - friendBefore) < 0.5f);
}

TEST_CASE("Messagebox command requires cheats and shows diagnostic and error pop-ups on the target player",
          "[sandbox][developer][messagebox]") {
    SandboxTests::Playground game;
    Spot::Console console;
    SandboxCommands commands(console, game.scene);
    auto* localMode = Gameplay::Players::FindComponent<DeveloperMode>(game.scene);
    REQUIRE(localMode);

    auto* friendEntity = game.scene.Instantiate(game.assets.Load<Spot::Prefab>("prefabs/actors/player.prefab"));
    REQUIRE(friendEntity);
    auto* friendMode = friendEntity->GetComponent<DeveloperMode>();
    REQUIRE(friendMode);
    auto& friendInfo = game.scene.CreateEntity("Friend Info").AddComponent<Gameplay::PlayerInfo>();
    friendInfo.name = "Friend";
    friendInfo.pawn = friendEntity->GetId();
    game.Run(2);

    std::vector<DeveloperMode::MessageBoxRecord> shown;
    Spot::MessageBox::SetHandler(
        [&](std::string_view title, std::string_view message, Spot::MessageBoxIcon icon, Spot::Window*) {
            shown.push_back({std::string(title), std::string(message), icon});
            return true;
        });

    CHECK_FALSE(console.Execute("messagebox Friend Hello"));
    CHECK(shown.empty());
    console.SetCheatsEnabled(true);
    CHECK_FALSE(console.Execute("messagebox"));
    CHECK_FALSE(console.Execute("messagebox Friend"));
    CHECK_FALSE(console.Execute("messagebox Friend error"));
    CHECK_FALSE(console.Execute("messagebox Nobody Hello"));
    CHECK(shown.empty());

    REQUIRE(console.Execute("messagebox Friend Hello there"));
    REQUIRE(shown.size() == 1);
    CHECK(shown.back().title == "Sandbox");
    CHECK(shown.back().message == "Hello there");
    CHECK(shown.back().icon == Spot::MessageBoxIcon::Information);
    REQUIRE(friendMode->GetLastMessageBox().has_value());
    CHECK(friendMode->GetLastMessageBox()->message == "Hello there");
    CHECK_FALSE(localMode->GetLastMessageBox().has_value());

    REQUIRE(console.Execute("messagebox Player error \"Out of memory\""));
    REQUIRE(shown.size() == 2);
    CHECK(shown.back().title == "Error");
    CHECK(shown.back().message == "Out of memory");
    CHECK(shown.back().icon == Spot::MessageBoxIcon::Error);
    REQUIRE(localMode->GetLastMessageBox().has_value());
    CHECK(localMode->GetLastMessageBox()->icon == Spot::MessageBoxIcon::Error);

    REQUIRE(console.Execute("messagebox Friend warning \"Disk Space\" \"Low disk space\""));
    REQUIRE(shown.size() == 3);
    CHECK(shown.back().title == "Disk Space");
    CHECK(shown.back().message == "Low disk space");
    CHECK(shown.back().icon == Spot::MessageBoxIcon::Warning);

    REQUIRE(console.Execute("messagebox all info \"Broadcast check\""));
    CHECK(shown.size() == 5);
    CHECK(localMode->GetLastMessageBox()->message == "Broadcast check");
    CHECK(friendMode->GetLastMessageBox()->message == "Broadcast check");

    Spot::MessageBox::SetHandler({});
}
