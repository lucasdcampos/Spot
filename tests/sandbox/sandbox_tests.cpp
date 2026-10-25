#include "../registered_assets.h"
#include <spot/scene/physics/physics_world.h>
// The sandbox's game code, run headless on its real scenes: the engine's integration test, which shows a regression
// as something broken in the game.
#include "day_cycle.h"
#include "explosion.h"
#include "explosive_barrel.h"
#include "flash.h"
#include "game_controls.h"
#include "log_capture.h"
#include "particle_effects.h"
#include "physics_gun.h"
#include "player_controller.h"
#include "player_movement.h"
#include "prop_carrier.h"
#include "prop_spawner.h"
#include "robot.h"
#include "robot_spawner.h"
#include "sandbox_commands.h"
#include "sound_bank.h"
#include "temp_directory.h"
#include "tool_gun.h"
#include "undo_list.h"
#include "view_model.h"
#include "weapon.h"
#include "weapon_holder.h"
#include "world_save.h"

#include <spot.h>
#include <spot_import.h>
#include <spot_tools.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using Catch::Approx;
using Spot::Entity;
using Spot::Key;
using Spot::Scene;

namespace {

constexpr float Step = Scene::DefaultFixedTimeStep;
constexpr auto Gamepad = static_cast<Spot::GamepadId>(1);
// An explicit fixture snapshot keeps acceptance runs independent of concurrent editor saves.
const std::filesystem::path Assets = [] {
    const auto snapshot = Spot::Process::GetEnvironmentValue("SPOT_SANDBOX_TEST_ASSETS");
    return snapshot && !snapshot->empty() ? std::filesystem::path(*snapshot)
                                          : std::filesystem::path(SANDBOX_ASSETS_DIR);
}();

// The playground, loaded from its source with the sandbox's components and the assets the build cooks for it, and
// input the test presses keys on, read through the actions of the game's settings, which the build writes beside
// them. There is no GPU, so meshes and fonts do not load, which the game's logic and physics do not need; the log,
// which says so, is kept from the output. With audio, the game sounds through a device mixed by hand as it runs, with
// the game's buses, and each run tells how loud each bus got.
struct Playground {
    std::shared_ptr<Spot::Tests::CaptureSink> log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    Spot::InputState input;
    Spot::InputActionMap actions;
    Spot::InputState* previousInput = Spot::Input::GetState();
    Spot::InputActionMap* previousActions = Spot::Input::GetActionMap();
    std::unique_ptr<Spot::AudioDevice> audio;
    Spot::AudioDevice* previousAudio = Spot::Audio::GetDevice();
    // The loudest sample of each bus over the last run, by name.
    std::map<std::string, Spot::AudioPeak, std::less<>> peaks;
    Spot::Tests::RegisteredAssetManager assets{SANDBOX_IMPORTED_DIR};
    Spot::AssetManager* previousAssets = Spot::Assets::GetManager();
    Scene scene;
    Entity* player = nullptr;

    explicit Playground(bool withAudio = false) {
        Spot::Detail::LoadGameCode();
        Spot::Detail::SetAssetManager(&assets);
        const auto game = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
        REQUIRE(game.has_value());
        game->sections.input.Apply(actions);
        REQUIRE(actions.Find("Fire") != nullptr);
        if (withAudio) {
            audio = std::make_unique<Spot::AudioDevice>(Spot::AudioConfig{.manualMix = true});
            game->sections.audio.Apply(*audio);
            Spot::Audio::SetDevice(audio.get());
        }
        Spot::Input::SetState(&input);
        Spot::Input::SetActionMap(&actions);
        REQUIRE(scene.LoadFromFile(Assets / "scenes" / "playground.scene", &assets));
        player = scene.FindEntity("Player");
        REQUIRE(player != nullptr);
    }

    ~Playground() {
        Spot::Input::SetState(previousInput);
        Spot::Input::SetActionMap(previousActions);
        // The scene's sounds stop while the device is still there.
        scene.Clear();
        Spot::Detail::SetAssetManager(previousAssets);
        Spot::Audio::SetDevice(previousAudio);
        // Components that threw were logged; nothing else in the game should have.
        for (const auto& record : log->GetRecords()) {
            CHECK(record.message.find(" threw in ") == std::string::npos);
        }
    }

    void Run(float seconds) {
        const auto frames = std::lround(seconds / Step);
        peaks.clear();
        std::vector<float> mix(static_cast<std::size_t>(Step * 48000.0f) * 2);
        for (long i = 0; i < frames; ++i) {
            actions.Update(input);
            scene.Update(Step);
            input.BeginFrame();
            if (audio) {
                audio->Render(mix);
                for (const Spot::AudioBusId bus : audio->GetBuses()) {
                    const Spot::AudioPeak peak = audio->GetBusPeak(bus);
                    Spot::AudioPeak& loudest = peaks[audio->GetBusName(bus)];
                    loudest = {std::max(loudest.left, peak.left), std::max(loudest.right, peak.right)};
                }
            }
        }
    }

    void Press(Key key) { input.OnEvent(Spot::KeyPressedEvent{.key = key, .repeat = false}); }
    void Release(Key key) { input.OnEvent(Spot::KeyReleasedEvent{.key = key}); }
    void Press(Spot::MouseButton button) { input.OnEvent(Spot::MouseButtonPressedEvent{.button = button}); }
    void Release(Spot::MouseButton button) { input.OnEvent(Spot::MouseButtonReleasedEvent{.button = button}); }
    void Scroll(float notches) { input.OnEvent(Spot::MouseScrolledEvent{.delta = {0.0f, notches}}); }
    void Press(Spot::GamepadButton button) {
        input.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = Gamepad, .button = button});
    }
    void Release(Spot::GamepadButton button) {
        input.OnEvent(Spot::GamepadButtonReleasedEvent{.gamepad = Gamepad, .button = button});
    }
    void Push(Spot::GamepadAxis axis, float value) {
        input.OnEvent(Spot::GamepadAxisEvent{.gamepad = Gamepad, .axis = axis, .value = value});
    }

    PlayerController& Player() const { return *player->GetComponent<PlayerController>(); }
    // A crate of the mass, the given distance along where the player looks.
    Entity& PlaceCrate(float distance, float mass) {
        auto& crate = scene.CreateEntity("Test Crate");
        crate.GetTransform().SetPosition(Player().GetEyePosition() + Player().GetAimDirection() * distance);
        crate.AddComponent<Spot::BoxCollider>(Spot::Vector3{0.5f, 0.5f, 0.5f});
        crate.AddComponent<Spot::Rigidbody>().SetMass(mass);
        crate.GetComponent<Spot::Rigidbody>()->SetGravityScale(0.0f);
        return crate;
    }
    Spot::Vector3 HeldTarget(float distance) const {
        return Player().GetEyePosition() + Player().GetAimDirection() * distance;
    }

    Spot::Vector3 PlayerPosition() const { return player->GetTransform().GetPosition(); }
    Spot::CharacterController& Controller() const { return *player->GetComponent<Spot::CharacterController>(); }
};

} // namespace

TEST_CASE("Generated robots patrol with blended root motion, footsteps and recoverable ragdolls",
          "[sandbox][animation]") {
    Playground playground;
    playground.Run(0.2f);
    REQUIRE(playground.scene.FindComponent<RobotSpawner>());
    auto* robot = playground.scene.FindComponent<Robot>();
    REQUIRE(robot);
    auto& entity = robot->GetEntity();
    auto* animator = entity.GetComponent<Spot::Animator>();
    REQUIRE(animator);
    REQUIRE(animator->GetController());
    CHECK(animator->GetController()->states[0].blendTree.type == Spot::BlendTreeType::OneDimensional);
    const auto start = entity.GetTransform().GetWorldPosition();
    playground.Run(1.0f);
    CHECK(Spot::Length(entity.GetTransform().GetWorldPosition() - start) > 0.3f);
    CHECK(robot->GetFootsteps() > 0);
    auto* ragdoll = entity.GetComponent<Spot::Ragdoll>();
    REQUIRE(ragdoll);
    const auto hitPosition = entity.GetTransform().GetWorldPosition();
    Explosion::Explode(playground.scene, hitPosition + Spot::Vector3{0, 0, 0.5f}, 1.5f, 50, 0);
    REQUIRE(robot->IsDead());
    REQUIRE(ragdoll->IsActive());
    REQUIRE(ragdoll->GetBodies().size() == 11);
    CHECK_FALSE(entity.GetComponent<Spot::CharacterController>()->IsEnabled());
    playground.Run(1.0f);
    CHECK(Spot::Length(entity.GetTransform().GetWorldPosition() - hitPosition) > 0.1f);
    robot->Revive();
    CHECK_FALSE(robot->IsDead());
    CHECK_FALSE(ragdoll->IsActive());
    playground.Run(0.5f);
    CHECK(animator->IsPlaying());
}

TEST_CASE("Hitscan shots turn robot capsules into physical skeletons", "[sandbox][animation]") {
    Playground playground;
    playground.Run(0.5f);
    auto prefab = playground.assets.Load<Spot::Prefab>("prefabs/actors/robot.prefab");
    REQUIRE(prefab);
    auto* target = playground.scene.Instantiate(prefab);
    REQUIRE(target);
    auto* robot = target->GetComponent<Robot>();
    REQUIRE(robot);
    robot->patrolDistance = 0;
    target->GetTransform().SetWorldPosition(playground.HeldTarget(3));
    playground.Run(Step);
    auto* holder = playground.player->GetComponent<WeaponHolder>();
    REQUIRE(holder);
    holder->Select(1);
    holder->Fire();
    CHECK(robot->IsDead());
    CHECK(target->GetComponent<Spot::Ragdoll>()->IsActive());
    playground.Run(0.3f);
}

TEST_CASE("World checkpoints restore robot populations and ragdoll ownership", "[sandbox][animation][save]") {
    Playground playground;
    TempDirectory directory;
    playground.Run(0.5f);
    auto* robot = playground.scene.FindComponent<Robot>();
    REQUIRE(robot);
    robot->GetEntity().SetName("Saved robot");
    robot->Hit({10, 0, 0}, robot->GetEntity().GetTransform().GetWorldPosition());
    playground.Run(0.2f);
    REQUIRE(robot->IsDead());
    REQUIRE(WorldSave::Save(playground.scene, directory.GetPath() / "robots.json"));
    robot->Revive();
    REQUIRE(WorldSave::Load(playground.scene, directory.GetPath() / "robots.json"));
    playground.Run(0.2f);
    auto* restored = playground.scene.FindEntity("Saved robot");
    REQUIRE(restored);
    CHECK(restored->GetComponent<Robot>()->IsDead());
    auto* ragdoll = restored->GetComponent<Spot::Ragdoll>();
    REQUIRE(ragdoll);
    REQUIRE(ragdoll->IsActive());
    REQUIRE(ragdoll->GetBodies().size() == 11);
    for (auto id : ragdoll->GetBodies()) {
        REQUIRE(playground.scene.GetEntity(id));
        CHECK(ragdoll->OwnsBody(id));
    }
    int robots = 0, populations = 0, bodies = 0;
    playground.scene.ForEachComponent<Robot>([&](Robot&) { ++robots; });
    playground.scene.ForEachComponent<RobotSpawner>([&](RobotSpawner&) { ++populations; });
    playground.scene.ForEachComponent<Spot::Ragdoll>(
        [&](Spot::Ragdoll& component) { bodies += static_cast<int>(component.GetBodies().size()); });
    CHECK(robots == 3);
    CHECK(populations == 1);
    CHECK(bodies == 11);
    restored->GetComponent<Robot>()->Revive();
    playground.Run(0.5f);
    CHECK(restored->GetComponent<Spot::Animator>()->IsPlaying());
}

TEST_CASE("Held items come up when switched to, fire from their muzzles and move their parts", "[sandbox][view]") {
    Playground playground;
    playground.Run(1.0f);
    auto* view = playground.player->GetComponent<ViewModel>();
    auto* holder = playground.player->GetComponent<WeaponHolder>();
    REQUIRE(view);
    REQUIRE(holder);
    const auto& player = playground.Player();
    // Each in turn, the physics gun, in hand first, last.
    for (int i = 1; i <= 6; ++i) {
        const int slot = i % 6;
        INFO("Slot " << slot);
        holder->Select(slot);
        playground.Run(Step);
        // Switching brings the item up from below.
        CHECK(view->GetRaise() < 0.5f);
        playground.Run(0.5f);
        CHECK(view->GetRaise() == Approx(1.0f));
        REQUIRE_FALSE(view->GetPartEntities().empty());
        // The muzzle is in front of the eyes, ahead of every part's origin, and below and right of the crosshair.
        const auto muzzle = view->GetMuzzlePosition() - player.GetEyePosition();
        const auto rotation = Spot::Inverse(player.GetViewRotation());
        const auto local = rotation * muzzle;
        CHECK(local.z < -0.3f);
        CHECK(local.y < 0.0f);
        CHECK(local.x > -0.05f);
    }

    // Down the sights, the sight is on the crosshair and the muzzle under it.
    holder->Select(1);
    playground.Run(0.5f);
    playground.Press(Spot::MouseButton::Right);
    playground.Run(0.4f);
    REQUIRE(holder->IsAiming());
    CHECK(view->GetAim() == Approx(1.0f));
    const auto aimed = Spot::Inverse(player.GetViewRotation()) * (view->GetMuzzlePosition() - player.GetEyePosition());
    CHECK(std::abs(aimed.x) < 0.01f);
    CHECK(aimed.y < 0.0f);
    CHECK(aimed.y > -0.05f);
    playground.Release(Spot::MouseButton::Right);
    playground.Run(0.4f);

    // The pistol's slide cycles back as it fires and returns.
    const auto slide = [&]() -> const Entity* {
        const auto parts = view->GetShownModel()->GetParts();
        for (std::size_t i = 0; i < parts.size() && i < view->GetPartEntities().size(); ++i) {
            if (parts[i].name == "Slide") {
                return playground.scene.GetEntity(view->GetPartEntities()[i]);
            }
        }
        return nullptr;
    };
    const auto local = [&](const Entity& entity) {
        return Spot::Inverse(player.GetViewRotation()) *
               (entity.GetTransform().GetWorldPosition() - player.GetEyePosition());
    };
    const Entity* part = slide();
    REQUIRE(part);
    {
        const auto rest = local(*part);
        holder->Fire();
        playground.Run(0.02f);
        CHECK(local(*part).z > rest.z + 0.005f);
        playground.Run(0.5f);
    }

    // Reloading drops the magazine out and back in, and rounds return when it ends.
    auto* weapon = holder->GetWeapon();
    REQUIRE(weapon);
    const int loaded = weapon->GetAmmo();
    playground.Press(Key::R);
    playground.Run(Step);
    playground.Release(Key::R);
    REQUIRE(weapon->IsReloading());
    playground.Run(weapon->reloadTime * 0.45f);
    CHECK(weapon->GetReloadProgress() > 0.3f);
    CHECK(weapon->GetReloadProgress() < 0.6f);
    playground.Press(Spot::MouseButton::Right);
    playground.Run(0.1f);
    CHECK_FALSE(holder->IsAiming());
    playground.Release(Spot::MouseButton::Right);
    playground.Run(weapon->reloadTime);
    CHECK_FALSE(weapon->IsReloading());
    CHECK(weapon->GetAmmo() > loaded);
}

TEST_CASE("World checkpoints retain edited props, joints, lamps and cleanup ownership", "[sandbox][save]") {
    Playground playground;
    TempDirectory directory;
    playground.Run(1.5f);
    auto& spawner = *playground.player->GetComponent<PropSpawner>();
    auto& tool = *playground.player->GetComponent<ToolGun>();
    auto* prop = spawner.Spawn(0);
    REQUIRE(prop);
    prop->SetName("Saved prop");
    prop->AddTag(PhysicsGun::FrozenTag);
    prop->GetComponent<Spot::Rigidbody>()->SetKinematic(true);
    const auto position = prop->GetTransform().GetPosition();
    const Spot::Color paint{0.2f, 0.8f, 0.1f, 1.0f};
    REQUIRE(tool.Paint(*prop, paint));
    auto& anchor = playground.PlaceCrate(5.0f, 5.0f);
    anchor.SetName("Saved anchor");
    anchor.GetComponent<Spot::Rigidbody>()->SetKinematic(true);
    REQUIRE(tool.Weld(*prop, anchor));
    auto& hanging = playground.PlaceCrate(3.0f, 5.0f);
    hanging.SetName("Saved rope");
    REQUIRE(tool.Rope(hanging, hanging.GetTransform().GetPosition(), anchor, anchor.GetTransform().GetPosition()));
    tool.color = 2;
    tool.lamp = ToolGun::LampKind::ShadowedPoint;
    auto* lamp = tool.PlaceLamp(*prop, position, {0.0f, 1.0f, 0.0f});
    REQUIRE(lamp);
    const auto lampColor = lamp->GetComponent<Spot::PointLight>()->GetColor();
    const auto playerId = playground.player->GetId();
    REQUIRE(WorldSave::Save(playground.scene, directory.GetPath() / "world.json"));
    spawner.Cleanup();
    anchor.Destroy();
    hanging.Destroy();
    REQUIRE(WorldSave::Load(playground.scene, directory.GetPath() / "world.json"));
    CHECK(playground.scene.GetEntity(playerId) == playground.player);
    CHECK(playground.player->GetComponent<UndoList>()->GetCount() == 0);
    auto* restored = playground.scene.FindEntity("Saved prop");
    auto* restoredAnchor = playground.scene.FindEntity("Saved anchor");
    auto* restoredRope = playground.scene.FindEntity("Saved rope");
    REQUIRE(restored);
    REQUIRE(restoredAnchor);
    REQUIRE(restoredRope);
    CHECK(restored->GetTransform().GetPosition() == position);
    CHECK(restored->HasTag(PhysicsGun::FrozenTag));
    CHECK(restored->GetComponent<Spot::Rigidbody>()->IsKinematic());
    CHECK(restored->GetComponent<Spot::MeshRenderer>()->GetMaterial()->baseColor == paint);
    CHECK(restored->GetComponent<Spot::FixedJoint>()->GetConnectedEntity() == restoredAnchor->GetId());
    CHECK(restoredRope->GetComponent<Spot::DistanceJoint>()->GetConnectedEntity() == restoredAnchor->GetId());
    const auto children = restored->GetChildren();
    const auto child =
        std::ranges::find_if(children, [](const Entity* entity) { return entity->HasTag(ToolGun::LampTag); });
    REQUIRE(child != children.end());
    CHECK((*child)->GetComponent<Spot::PointLight>()->GetColor() == lampColor);
    CHECK((*child)->GetComponent<Spot::PointLight>()->IsCastingShadows());
    CHECK(spawner.GetSpawnedCount() == 1);
    spawner.Cleanup();
    CHECK_FALSE(playground.scene.FindEntity("Saved prop"));
    CHECK(playground.scene.FindEntity("Saved anchor"));
}

TEST_CASE("Quick save and load use the project's bindings and reject invalid checkpoints", "[sandbox][save]") {
    Playground playground;
    TempDirectory directory;
    playground.Run(1.0f);
    auto* controls = playground.scene.FindComponent<GameControls>();
    REQUIRE(controls);
    controls->SetSaveDirectory(directory.GetPath());
    auto& spawner = *playground.player->GetComponent<PropSpawner>();
    REQUIRE(spawner.Spawn(0));
    playground.Press(Key::F9);
    playground.Run(Step);
    playground.Release(Key::F9);
    REQUIRE(std::filesystem::is_regular_file(directory.GetPath() / "world.json"));
    spawner.Cleanup();
    CHECK(spawner.GetSpawnedCount() == 0);
    playground.Press(Key::F10);
    playground.Run(Step);
    playground.Release(Key::F10);
    CHECK(spawner.GetSpawnedCount() == 1);
    const auto count = playground.scene.GetRootEntities().size();
    CHECK(WorldSave::Load(playground.scene, directory.GetPath() / "missing.json").status ==
          Spot::SaveDataStatus::NotFound);
    Spot::SaveData saves(directory.GetPath());
    REQUIRE(saves.Save("invalid.json", 1, [](Spot::Archive&, std::uint32_t) {}));
    CHECK(WorldSave::Load(playground.scene, directory.GetPath() / "invalid.json").status ==
          Spot::SaveDataStatus::InvalidFile);
    CHECK(playground.scene.GetRootEntities().size() == count);
}

TEST_CASE("Saved lit barrels resume their remaining fuse", "[sandbox][save]") {
    Playground playground;
    TempDirectory directory;
    playground.Run(1.0f);
    auto& barrel = playground.scene.CreateEntity("Saved fuse");
    barrel.GetTransform().SetPosition({200.0f, 5.0f, 0.0f});
    barrel.AddComponent<Spot::Rigidbody>();
    auto& explosive = barrel.AddComponent<ExplosiveBarrel>();
    explosive.fuse = 1.0f;
    explosive.Ignite();
    playground.Run(0.6f);
    REQUIRE(WorldSave::Save(playground.scene, directory.GetPath() / "world.json"));
    REQUIRE(WorldSave::Load(playground.scene, directory.GetPath() / "world.json"));
    REQUIRE(playground.scene.FindEntity("Saved fuse"));
    CHECK(playground.scene.FindEntity("Saved fuse")->GetComponent<ExplosiveBarrel>()->IsLit());
    playground.scene.Update(0.0f);
    playground.Run(0.2f);
    CHECK(playground.scene.FindEntity("Saved fuse"));
    playground.Run(0.3f);
    CHECK_FALSE(playground.scene.FindEntity("Saved fuse"));
}

TEST_CASE("Sandbox recoil uses cancellable tweens, and walking steps and bobs at a natural cadence",
          "[sandbox][gameplay]") {
    Playground playground;
    playground.Run(1.5f);
    auto& view = *playground.player->GetComponent<ViewModel>();
    view.Kick();
    playground.scene.Update(0.0f);
    CHECK(view.GetRecoil() == 0.0f);
    playground.Run(0.05f);
    CHECK(view.GetRecoil() > 0.0f);
    playground.Run(0.25f);
    CHECK(view.GetRecoil() == Approx(0.0f));
    auto& movement = *playground.player->GetComponent<PlayerMovement>();
    const float start = movement.GetStepPhase();
    playground.Press(Key::W);
    float peak = 0.0f;
    for (int i = 0; i < 120; ++i) {
        playground.Run(Step);
        peak = std::max(peak, std::abs(movement.GetCameraBob()));
    }
    // A gentle dip at each step, about two steps a second at walking speed.
    CHECK(peak > movement.bobHeight * 0.5f);
    CHECK(peak < movement.bobHeight * 1.5f);
    const float steps = movement.GetStepPhase() - start;
    CHECK(steps > 3.5f);
    CHECK(steps < 6.0f);
    playground.Release(Key::W);
    playground.Run(0.8f);
    CHECK(movement.GetCameraBob() == Approx(0.0f).margin(0.0005f));
}

TEST_CASE("The playground runs without faults", "[sandbox]") {
    Playground playground;
    playground.Run(5.0f);
    playground.scene.ForEachEntity([](Entity& entity) {
        for (const Spot::Component* component : entity.GetComponents<Spot::Component>()) {
            CAPTURE(entity.GetName());
            CHECK_FALSE(component->IsFaulted());
        }
    });
}

TEST_CASE("The player lands, walks where it looks and jumps", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    REQUIRE(playground.Controller().IsGrounded());
    const Spot::Vector3 start = playground.PlayerPosition();
    // The capsule, 1.8 tall, stands on the ground's top at y = 0.
    CHECK(start.y == Approx(0.9f).margin(0.05f));

    playground.Press(Key::W);
    playground.Run(1.0f);
    playground.Release(Key::W);
    const Spot::Vector3 walked = playground.PlayerPosition();
    // It looks down -Z, and walks at 5 meters per second.
    CHECK(start.z - walked.z == Approx(5.0f).margin(0.5f));
    CHECK(std::abs(walked.x - start.x) < 0.1f);

    playground.Press(Key::Space);
    playground.Run(0.3f);
    CHECK(playground.PlayerPosition().y > start.y + 0.5f);
    CHECK_FALSE(playground.Controller().IsGrounded());
    playground.Run(1.5f);
    CHECK(playground.Controller().IsGrounded());
    CHECK(playground.PlayerPosition().y == Approx(start.y).margin(0.05f));
}

TEST_CASE("Mouse look turns the camera and movement follows the visible direction", "[sandbox][camera]") {
    Playground playground;
    playground.Run(1.5f);
    auto* head = playground.player->FindChild("Head");
    REQUIRE(head);
    REQUIRE(playground.Player().head == head->GetId());
    auto* camera = head->GetComponent<Spot::Camera>();
    REQUIRE(camera);
    const auto initialForward = camera->GetCamera3D().GetForward();
    playground.input.OnEvent(Spot::MouseMovedEvent{
        .position = {}, .delta = {Spot::ToRadians(90.0f) / playground.Player().lookSensitivity, -100.0f}});
    playground.Run(Step);
    const auto view = camera->GetCamera3D();
    CHECK(Spot::Length(view.GetForward() - initialForward) > 1.0f);
    CHECK(Spot::Length(view.GetForward() - playground.Player().GetAimDirection()) < 0.001f);
    CHECK(Spot::Length(view.position - playground.Player().GetEyePosition()) < 0.001f);

    Spot::Vector3 direction;
    Key key = Key::W;
    SECTION("Forward") {
        direction = view.GetForward();
    }
    SECTION("Backward") {
        key = Key::S;
        direction = -view.GetForward();
    }
    SECTION("Right") {
        key = Key::D;
        direction = view.GetRight();
    }
    SECTION("Left") {
        key = Key::A;
        direction = -view.GetRight();
    }
    direction.y = 0.0f;
    direction = Spot::Normalize(direction);
    const auto start = playground.PlayerPosition();
    playground.Press(key);
    playground.Run(0.25f);
    playground.Release(key);
    const auto displacement = playground.PlayerPosition() - start;
    CHECK(Spot::Dot(displacement, direction) == Approx(1.25f).margin(0.15f));
    CHECK(Spot::Length(displacement - direction * Spot::Dot(displacement, direction)) < 0.05f);
    CHECK(Spot::Length(camera->GetCamera3D().position - playground.Player().GetEyePosition()) < 0.001f);

    playground.player->GetComponent<PlayerMovement>()->SetFlying(true);
    const auto flightStart = playground.PlayerPosition();
    playground.Press(Key::W);
    playground.Run(0.1f);
    playground.Release(Key::W);
    CHECK(Spot::Length(playground.PlayerPosition() - flightStart - view.GetForward()) < 0.05f);

    auto* flashlight = playground.scene.GetEntity(playground.Player().flashlight);
    REQUIRE(flashlight);
    CHECK(flashlight->GetParent() == head);
    playground.Press(Key::F);
    playground.Run(Step);
    CHECK(flashlight->GetComponent<Spot::Light>()->IsEnabled());
}

TEST_CASE("The player crouches, slower and lower, and stands only with room above", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    const Spot::Vector3 start = playground.PlayerPosition();

    playground.Press(Key::LeftControl);
    playground.Press(Key::W);
    playground.Run(1.0f);
    playground.Release(Key::W);
    CHECK(playground.Controller().GetHeight() == Approx(1.0f));
    // Crouching walks at 2.5 meters per second, with the feet where they were.
    CHECK(start.z - playground.PlayerPosition().z == Approx(2.5f).margin(0.3f));
    CHECK(playground.PlayerPosition().y == Approx(start.y).margin(0.05f));

    // A ceiling just above the crouching player keeps it down.
    auto& ceiling = playground.scene.CreateEntity("Ceiling");
    ceiling.GetTransform().SetPosition(playground.PlayerPosition() + Spot::Vector3{0.0f, 0.75f, 0.0f});
    ceiling.AddComponent<Spot::BoxCollider>(Spot::Vector3{3.0f, 0.2f, 3.0f});
    playground.Run(0.1f);
    playground.Release(Key::LeftControl);
    playground.Run(0.5f);
    CHECK(playground.Controller().GetHeight() == Approx(1.0f));

    ceiling.Destroy();
    playground.Run(0.5f);
    CHECK(playground.Controller().GetHeight() == Approx(1.8f));
}

TEST_CASE("Noclip flies the player through everything, and walking lands it again", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    const float ground = playground.PlayerPosition().y;

    playground.Press(Key::LeftAlt);
    playground.Run(0.1f);
    playground.Release(Key::LeftAlt);
    playground.Press(Key::Space);
    playground.Run(1.0f);
    playground.Release(Key::Space);
    // Ten meters per second, straight up, with nothing pulling it down.
    CHECK(playground.PlayerPosition().y == Approx(ground + 10.0f).margin(1.0f));
    playground.Run(1.0f);
    CHECK(playground.PlayerPosition().y == Approx(ground + 10.0f).margin(1.0f));

    playground.Press(Key::LeftAlt);
    playground.Run(3.0f);
    CHECK(playground.Controller().IsGrounded());
    CHECK(playground.PlayerPosition().y == Approx(ground).margin(0.05f));
}

TEST_CASE("The player climbs the stairs and the ramps of the level's meshes", "[sandbox]") {
    Playground playground;
    // At the foot of the proving yard's steps, which rise toward -X onto the platform, whose top is at y = 2.
    playground.player->GetTransform().SetPosition({-37.5f, 0.95f, -12.0f});
    playground.Run(0.5f);
    playground.Press(Key::A);
    playground.Run(1.5f);
    playground.Release(Key::A);
    CHECK(playground.PlayerPosition().x < -43.5f);
    CHECK(playground.PlayerPosition().y == Approx(2.9f).margin(0.1f));

    // At the foot of the jump ramp, which rises toward -Z to a height of 2 over six meters.
    playground.player->GetTransform().SetPosition({-30.0f, 0.95f, 8.0f});
    playground.Run(0.5f);
    playground.Press(Key::W);
    playground.Run(0.7f);
    CHECK(playground.PlayerPosition().y > 1.3f);
    CHECK(playground.Controller().IsGrounded());
}

TEST_CASE("The player picks up light props, carries them and throws them", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    auto& carrier = *playground.player->GetComponent<PropCarrier>();
    Entity& heavy = playground.PlaceCrate(1.8f, 200.0f);
    playground.Run(0.1f);
    playground.Press(Key::E);
    playground.Run(0.1f);
    playground.Release(Key::E);
    CHECK_FALSE(carrier.IsHolding());
    heavy.Destroy();

    Entity& crate = playground.PlaceCrate(2.0f, 10.0f);
    crate.GetComponent<Spot::Rigidbody>()->SetGravityScale(1.0f);
    playground.Run(0.05f);
    playground.Press(Key::E);
    playground.Run(0.05f);
    playground.Release(Key::E);
    REQUIRE(carrier.IsHolding());
    CHECK(carrier.GetHeld() == crate.GetId());
    playground.Run(1.0f);
    // Carried in front of the camera, against gravity.
    CHECK(Spot::Distance(crate.GetTransform().GetPosition(), playground.HeldTarget(carrier.holdDistance)) < 0.15f);

    playground.Press(Spot::MouseButton::Left);
    playground.Run(0.05f);
    playground.Release(Spot::MouseButton::Left);
    CHECK_FALSE(carrier.IsHolding());
    const Spot::Vector3 velocity = crate.GetComponent<Spot::Rigidbody>()->GetLinearVelocity();
    CHECK(Spot::Dot(velocity, playground.Player().GetAimDirection()) > 8.0f);
}

TEST_CASE("The physics gun grabs props from afar, pulls and pushes them, and freezes them", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    auto& gun = *playground.player->GetComponent<PhysicsGun>();
    playground.Press(Key::Digit1);
    playground.Run(0.05f);
    playground.Release(Key::Digit1);
    CHECK(playground.player->GetComponent<WeaponHolder>()->IsPhysicsGunSelected());

    Entity& crate = playground.PlaceCrate(6.0f, 50.0f);
    crate.GetComponent<Spot::Rigidbody>()->SetGravityScale(1.0f);
    playground.Run(0.05f);
    playground.Press(Spot::MouseButton::Left);
    playground.Run(0.05f);
    REQUIRE(gun.IsGrabbing());
    // Held up against gravity, where it was grabbed.
    playground.Run(1.0f);
    const float distance = Spot::Distance(crate.GetTransform().GetPosition(), playground.Player().GetEyePosition());
    CHECK(distance == Approx(6.0f).margin(0.5f));

    // The wheel pulls it three meters closer.
    playground.Scroll(-3.0f);
    playground.Run(1.0f);
    CHECK(Spot::Distance(crate.GetTransform().GetPosition(), playground.Player().GetEyePosition()) ==
          Approx(3.0f).margin(0.5f));

    playground.Press(Spot::MouseButton::Right);
    playground.Run(0.05f);
    playground.Release(Spot::MouseButton::Right);
    playground.Release(Spot::MouseButton::Left);
    CHECK_FALSE(gun.IsGrabbing());
    CHECK(crate.HasTag(PhysicsGun::FrozenTag));
    const Spot::Vector3 frozen = crate.GetTransform().GetPosition();
    playground.Run(1.0f);
    CHECK(Spot::Distance(crate.GetTransform().GetPosition(), frozen) < 0.01f);

    playground.Press(Key::U);
    playground.Run(1.0f);
    CHECK_FALSE(crate.HasTag(PhysicsGun::FrozenTag));
    // It fell to the ground, its half size above it.
    CHECK(crate.GetTransform().GetPosition().y < frozen.y - 0.5f);
    CHECK(crate.GetTransform().GetPosition().y == Approx(0.25f).margin(0.05f));
}

namespace {

// Selects the weapon of the slot and clicks once.
void FireSlot(Playground& playground, Key slot) {
    playground.Press(slot);
    playground.Run(0.05f);
    playground.Release(slot);
    playground.Press(Spot::MouseButton::Left);
    playground.Run(0.05f);
    playground.Release(Spot::MouseButton::Left);
}

} // namespace

TEST_CASE("Hitscan weapons push what they hit, and run out of rounds until reloaded", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    auto& holder = *playground.player->GetComponent<WeaponHolder>();
    REQUIRE(holder.GetWeapons().size() == 4);

    Entity& crate = playground.PlaceCrate(5.0f, 5.0f);
    playground.Run(0.05f);
    FireSlot(playground, Key::Digit2);
    REQUIRE(holder.GetWeapon() != nullptr);
    CHECK(holder.GetWeapon()->displayName == "Pistol");
    const float pistolSpeed =
        Spot::Dot(crate.GetComponent<Spot::Rigidbody>()->GetLinearVelocity(), playground.Player().GetAimDirection());
    // Forty newton seconds on five kilograms.
    CHECK(pistolSpeed == Approx(8.0f).margin(1.0f));
    CHECK(holder.GetWeapon()->GetAmmo() == 11);

    // The pistol fires once per click, every fifth of a second, until its twelve rounds are gone.
    for (int i = 0; i < 15; ++i) {
        playground.Press(Spot::MouseButton::Left);
        playground.Run(0.05f);
        playground.Release(Spot::MouseButton::Left);
        playground.Run(0.2f);
    }
    playground.Run(1.5f);
    // Out of rounds, it reloaded by itself meanwhile.
    CHECK(holder.GetWeapon()->GetReserve() < 120);

    // The rifle keeps firing while the button is held: ten rounds a second.
    crate.Destroy();
    playground.Press(Key::Digit3);
    playground.Run(0.05f);
    playground.Release(Key::Digit3);
    playground.Press(Spot::MouseButton::Left);
    playground.Run(1.0f);
    playground.Release(Spot::MouseButton::Left);
    CHECK(holder.GetWeapon()->GetAmmo() == Approx(20).margin(2));

    // The shotgun's pellets push a crate harder than the pistol did.
    Entity& target = playground.PlaceCrate(4.0f, 5.0f);
    playground.Run(0.05f);
    FireSlot(playground, Key::Digit4);
    const float shotgunSpeed =
        Spot::Dot(target.GetComponent<Spot::Rigidbody>()->GetLinearVelocity(), playground.Player().GetAimDirection());
    CHECK(shotgunSpeed > pistolSpeed * 1.5f);
}

TEST_CASE("Rockets and explosive barrels blow props away", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    // A crate on the ground some way ahead, and a rocket fired at the ground beside it.
    Entity& crate = playground.PlaceCrate(8.0f, 5.0f);
    crate.GetComponent<Spot::Rigidbody>()->SetGravityScale(1.0f);
    playground.Run(1.0f);
    const Spot::Vector3 before = crate.GetTransform().GetPosition();
    FireSlot(playground, Key::Digit5);
    playground.Run(1.0f);
    CHECK(playground.scene.FindEntity("Rocket") == nullptr);
    CHECK(Spot::Distance(crate.GetTransform().GetPosition(), before) > 1.0f);

    // Shooting an explosive barrel sets it off, pushing the props around it.
    Entity* barrel = playground.scene.FindEntity("Explosive Barrel");
    REQUIRE(barrel != nullptr);
    const Spot::EntityId barrelId = barrel->GetId();
    Entity& neighbor = playground.scene.CreateEntity("Neighbor");
    neighbor.GetTransform().SetPosition(barrel->GetTransform().GetPosition() + Spot::Vector3{1.5f, 0.0f, 0.0f});
    neighbor.AddComponent<Spot::BoxCollider>(Spot::Vector3{0.5f, 0.5f, 0.5f});
    neighbor.AddComponent<Spot::Rigidbody>().SetMass(5.0f);
    playground.Run(0.5f);
    const Spot::Vector3 neighborBefore = neighbor.GetTransform().GetPosition();
    barrel->GetComponent<ExplosiveBarrel>()->Ignite();
    playground.Run(0.5f);
    CHECK(playground.scene.GetEntity(barrelId) == nullptr);
    CHECK(Spot::Distance(neighbor.GetTransform().GetPosition(), neighborBefore) > 1.0f);
}

TEST_CASE("The playground uses linked spawnable prefabs with its authored overrides", "[sandbox][prefab]") {
    Playground playground;
    std::size_t linked = 0;
    playground.scene.ForEachComponent<Spawnable>([&](Spawnable& spawnable) {
        Entity& entity = spawnable.GetEntity();
        CHECK(entity.GetPrefabAssetId().IsValid());
        CHECK(entity.GetPrefabRootId() == entity.GetId());
        ++linked;
        if (spawnable.category == Spawnable::Category::Weapon) {
            CHECK(entity.GetParent() == playground.player);
            CHECK(entity.HasComponent<Weapon>());
            CHECK_FALSE(entity.HasComponent<Spot::Rigidbody>());
            CHECK_FALSE(entity.HasComponent<Spot::BoxCollider>());
            playground.scene.ForEachComponent<Spot::MeshRenderer>(
                [&](Spot::MeshRenderer& renderer) { CHECK_FALSE(renderer.GetEntity().IsDescendantOf(entity)); });
        }
    });
    CHECK(linked == 71);
    const Entity* cone = playground.scene.FindEntity("Cone");
    REQUIRE(cone != nullptr);
    CHECK(cone->HasComponent<Spot::BoxCollider>());
    CHECK_FALSE(cone->HasComponent<Spot::MeshCollider>());
    const Entity* lamp = playground.scene.FindEntity("Lamp");
    REQUIRE(lamp != nullptr);
    CHECK(lamp->HasComponent<Spot::PointLight>());
    CHECK_FALSE(lamp->HasComponent<Spot::Rigidbody>());
}

TEST_CASE("Every catalog entry spawns its complete prefab and undo removes its tree", "[sandbox][prefab]") {
    Playground playground;
    playground.Run(Step);
    auto& spawner = *playground.player->GetComponent<PropSpawner>();
    auto& undo = *playground.player->GetComponent<UndoList>();
    REQUIRE(spawner.GetSpawnables().size() == 17);
    const std::vector<std::string> shortcuts{"Crate", "Barrel", "Explosive Barrel", "Ball", "Plank", "Beam",
                                             "Cone",  "Wheel"};
    for (std::size_t i = 0; i < spawner.GetSpawnables().size(); ++i) {
        const SpawnableItem& item = spawner.GetSpawnables()[i];
        if (i < shortcuts.size()) {
            CHECK(item.name == shortcuts[i]);
        }
        Entity* spawned = spawner.Spawn(i);
        REQUIRE(spawned != nullptr);
        CHECK(spawned->GetPrefabAssetId() == item.prefab.GetId());
        CHECK(spawned->HasComponent<Spawnable>());
        CHECK(spawned->HasComponent<Spot::Rigidbody>());
        CHECK(spawned->HasComponent<ImpactSounds>());
        CHECK(spawned->HasComponent<Surface>());
        if (item.category == Spawnable::Category::Lamp) {
            CHECK(spawned->HasComponent<Spot::PointLight>());
        }
        std::vector<Spot::EntityId> tree{spawned->GetId()};
        playground.scene.ForEachEntity([&](Entity& entity) {
            if (entity.IsDescendantOf(*spawned)) {
                tree.push_back(entity.GetId());
            }
        });
        if (item.category == Spawnable::Category::Weapon) {
            CHECK(spawned->HasComponent<Weapon>());
            CHECK(tree.size() > 1);
            bool drawable = false;
            playground.scene.ForEachComponent<Spot::MeshRenderer>([&](Spot::MeshRenderer& renderer) {
                drawable = drawable || renderer.GetEntity().IsDescendantOf(*spawned);
            });
            CHECK(drawable);
        }
        CHECK(undo.Undo());
        for (const auto id : tree) {
            CHECK(playground.scene.GetEntity(id) == nullptr);
        }
    }
    CHECK(spawner.GetSpawnedCount() == 0);
    CHECK(spawner.Spawn(spawner.GetSpawnables().size()) == nullptr);
}

TEST_CASE("Adding a marked prefab asset extends the sandbox catalog without code changes", "[sandbox][prefab]") {
    Playground playground;
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    Spot::Tests::RegisteredAssetManager assets(imported);
    auto& spawner = *playground.player->GetComponent<PropSpawner>();
    Scene authored;
    Entity& root = authored.CreateEntity("Custom Lamp");
    auto& marker = root.AddComponent<Spawnable>();
    marker.category = Spawnable::Category::Lamp;
    marker.clearance = 2.0f;
    root.AddComponent<Spot::PointLight>();
    Entity& child = authored.CreateEntity("Attachment");
    child.SetParent(&root);
    child.AddComponent<Spot::BoxCollider>();
    const auto write = [&](const std::filesystem::path& path) {
        std::filesystem::create_directories((sources / path).parent_path());
        REQUIRE(authored.SaveToFile(sources / path));
    };
    write("prefabs/spawnables/custom.prefab");
    write("prefabs/spawnables_extra/outside.prefab");
    root.RemoveComponent(marker);
    write("prefabs/spawnables/unmarked.prefab");
    Spot::AssetDatabase database(sources, imported);
    database.Refresh();
    assets.RefreshManifest();
    spawner.RefreshSpawnables(&assets);
    REQUIRE(spawner.GetSpawnables().size() == 1);
    CHECK(spawner.GetSpawnables()[0].name == "Custom Lamp");
    CHECK(spawner.GetSpawnables()[0].category == Spawnable::Category::Lamp);
    CHECK(spawner.GetSpawnables()[0].clearance == Approx(2.0f));
    Entity* spawned = spawner.Spawn(0);
    REQUIRE(spawned != nullptr);
    CHECK(spawned->GetPrefabAssetId() == database.FindByPath("prefabs/spawnables/custom.prefab"));
    CHECK(spawned->HasComponent<Spot::PointLight>());
    REQUIRE(spawned->GetChildren().size() == 1);
    const auto childId = spawned->GetChildren()[0]->GetId();
    CHECK(spawned->GetChildren()[0]->HasComponent<Spot::BoxCollider>());
    spawner.Cleanup();
    CHECK(playground.scene.GetEntity(childId) == nullptr);
    spawner.RefreshSpawnables();
    CHECK(spawner.GetSpawnables().size() == 17);
}

TEST_CASE("Sandbox prefab edits propagate while instance overrides survive", "[sandbox][prefab]") {
    Playground playground;
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    std::filesystem::create_directories(sources);
    const auto sourceFile = sources / "crate.prefab";
    std::filesystem::copy_file(Assets / "prefabs/spawnables/props/crate.prefab", sourceFile);
    std::filesystem::copy_file(Assets / "prefabs/spawnables/props/crate.prefab.meta", sources / "crate.prefab.meta");
    Spot::AssetDatabase database(sources, imported);
    database.Refresh();
    Spot::Tests::RegisteredAssetManager assets(imported);
    const auto id = *database.FindByPath("crate.prefab");
    Entity* original = playground.scene.FindEntity("Crate");
    REQUIRE(original != nullptr);
    Scene instances;
    auto roots = instances.Instantiate(original->SaveToString(), {.assets = &assets});
    REQUIRE(roots.size() == 1);
    Entity* first = roots[0];
    Entity* second = instances.Instantiate(assets.Load<Spot::Prefab>(id), {.assets = &assets});
    REQUIRE(second != nullptr);
    const auto firstId = first->GetId();
    const auto secondId = second->GetId();
    const auto firstPosition = first->GetTransform().GetPosition();
    second->SetName("Personal Crate");
    second->GetComponent<ImpactSounds>()->minSpeed = 3.0f;
    const auto saved = Spot::SaveSceneSource(instances, &assets);

    Scene authored;
    auto sourceRoots =
        authored.Instantiate(assets.Load<Spot::Prefab>(id)->GetEntityText(), {.keepIds = true, .assets = &assets});
    REQUIRE(sourceRoots.size() == 1);
    sourceRoots[0]->SetName("Updated Crate");
    sourceRoots[0]->GetComponent<ImpactSounds>()->minSpeed = 2.0f;
    sourceRoots[0]->AddComponent<Spot::PointLight>();
    REQUIRE(authored.SaveToFile(sourceFile));
    REQUIRE(database.Reimport(id));
    const std::array changed{id};
    Spot::DevToolsModule::ReloadAssets(assets, changed);
    Scene refreshed;
    REQUIRE_FALSE(refreshed.Instantiate(saved, {.keepIds = true, .assets = &assets}).empty());
    first = refreshed.GetEntity(firstId);
    second = refreshed.GetEntity(secondId);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->GetName() == "Updated Crate");
    CHECK(first->GetTransform().GetPosition() == firstPosition);
    CHECK(first->GetComponent<ImpactSounds>()->minSpeed == Approx(2.0f));
    CHECK(second->GetName() == "Personal Crate");
    CHECK(second->GetComponent<ImpactSounds>()->minSpeed == Approx(3.0f));
    CHECK(first->HasComponent<Spot::PointLight>());
    CHECK(second->HasComponent<Spot::PointLight>());
    CHECK(first->GetPrefabAssetId() == id);
    CHECK(second->GetPrefabAssetId() == id);
}

TEST_CASE("Keys spawn props where the player looks, Z takes them back and X cleans them up", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    auto& spawner = *playground.player->GetComponent<PropSpawner>();
    REQUIRE(spawner.GetSpawnables().size() >= 8);
    for (const Key key : {Key::F1, Key::F4, Key::F7}) {
        playground.Press(key);
        playground.Run(0.05f);
        playground.Release(key);
    }
    CHECK(spawner.GetSpawnedCount() == 3);
    Entity* crate = playground.scene.FindEntity("Crate");
    REQUIRE(crate != nullptr);
    playground.Run(1.0f);
    // The spawned cone collides through its mesh's convex hull; the level's own cones have boxes.
    bool convexCone = false;
    playground.scene.ForEachComponent<Spot::MeshCollider>([&convexCone](Spot::MeshCollider& collider) {
        convexCone = convexCone || (collider.GetEntity().GetName() == "Cone" && collider.IsConvex());
    });
    CHECK(convexCone);

    playground.Press(Key::Z);
    playground.Run(0.05f);
    playground.Release(Key::Z);
    CHECK(spawner.GetSpawnedCount() == 2);
    playground.Press(Key::X);
    playground.Run(0.05f);
    playground.Release(Key::X);
    CHECK(spawner.GetSpawnedCount() == 0);
}

TEST_CASE("The tool gun welds, ropes and removes props, and Z takes it all back", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    auto& tool = *playground.player->GetComponent<ToolGun>();
    auto& undo = *playground.player->GetComponent<UndoList>();
    playground.Press(Key::Digit6);
    playground.Run(0.05f);
    playground.Release(Key::Digit6);
    REQUIRE(playground.player->GetComponent<WeaponHolder>()->IsToolGunSelected());

    // A crate welded in the air to a frozen one stays there.
    Entity& anchor = playground.PlaceCrate(4.0f, 5.0f);
    // High enough for what hangs below it to clear the ground.
    anchor.GetTransform().SetPosition(anchor.GetTransform().GetPosition() + Spot::Vector3{0.0f, 3.0f, 0.0f});
    anchor.GetComponent<Spot::Rigidbody>()->SetKinematic(true);
    Entity& welded = playground.PlaceCrate(4.0f, 5.0f);
    welded.GetTransform().SetPosition(anchor.GetTransform().GetPosition() + Spot::Vector3{0.5f, 0.0f, 0.0f});
    welded.GetComponent<Spot::Rigidbody>()->SetGravityScale(1.0f);
    REQUIRE(tool.Weld(welded, anchor) != nullptr);
    playground.Run(1.0f);
    CHECK(Spot::Distance(welded.GetTransform().GetPosition(), anchor.GetTransform().GetPosition()) ==
          Approx(0.5f).margin(0.02f));

    // Undoing the weld drops the crate.
    const float heldAt = welded.GetTransform().GetPosition().y;
    REQUIRE(undo.Undo());
    playground.Run(0.5f);
    CHECK(welded.GetTransform().GetPosition().y < heldAt - 0.5f);

    // A rope from the frozen crate hangs a crate a meter below it, at most.
    Entity& hanging = playground.PlaceCrate(4.0f, 5.0f);
    hanging.GetTransform().SetPosition(anchor.GetTransform().GetPosition() + Spot::Vector3{0.0f, -1.0f, 0.0f});
    hanging.GetComponent<Spot::Rigidbody>()->SetGravityScale(1.0f);
    const Spot::Vector3 top = hanging.GetTransform().GetPosition() + Spot::Vector3{0.0f, 0.25f, 0.0f};
    const Spot::Vector3 bottom = anchor.GetTransform().GetPosition() - Spot::Vector3{0.0f, 0.25f, 0.0f};
    REQUIRE(tool.Rope(hanging, top, anchor, bottom) != nullptr);
    playground.Run(1.0f);
    CHECK(anchor.GetTransform().GetPosition().y - hanging.GetTransform().GetPosition().y == Approx(1.0f).margin(0.05f));

    // The remover takes a prop away, the click on it as the player aims; the level stays.
    playground.Press(Key::R);
    playground.Run(0.05f);
    playground.Release(Key::R);
    playground.Press(Key::R);
    playground.Run(0.05f);
    playground.Release(Key::R);
    REQUIRE(tool.mode == ToolGun::Mode::Remover);
    Entity& doomed = playground.PlaceCrate(3.0f, 5.0f);
    const Spot::EntityId doomedId = doomed.GetId();
    playground.Run(0.05f);
    playground.Press(Spot::MouseButton::Left);
    playground.Run(0.05f);
    playground.Release(Spot::MouseButton::Left);
    CHECK(playground.scene.GetEntity(doomedId) == nullptr);
    Entity* ground = playground.scene.FindEntity("Ground");
    REQUIRE(ground != nullptr);
    CHECK_FALSE(tool.Remove(*ground));

    // Z brings the removed crate back, with its id.
    playground.Press(Key::Z);
    playground.Run(0.05f);
    playground.Release(Key::Z);
    CHECK(playground.scene.GetEntity(doomedId) != nullptr);
}

TEST_CASE("The whole game plays with a gamepad", "[sandbox][gamepad]") {
    using Spot::GamepadAxis;
    using Spot::GamepadButton;
    Playground playground;
    playground.Run(1.5f);
    auto& holder = *playground.player->GetComponent<WeaponHolder>();
    const Spot::Vector3 start = playground.PlayerPosition();

    // The left stick walks.
    playground.Push(GamepadAxis::LeftY, 1.0f);
    playground.Run(1.0f);
    playground.Push(GamepadAxis::LeftY, 0.0f);
    CHECK(start.z - playground.PlayerPosition().z == Approx(5.0f).margin(0.5f));

    // The right stick turns, as a mouse moving ten pixels a frame would.
    const float yaw = playground.Player().GetYaw();
    playground.Push(GamepadAxis::RightX, 1.0f);
    playground.Run(0.5f);
    playground.Push(GamepadAxis::RightX, 0.0f);
    CHECK(playground.Player().GetYaw() == Approx(yaw - 30 * 10.0f * playground.Player().lookSensitivity).margin(0.05f));

    // South jumps.
    const float ground = playground.PlayerPosition().y;
    playground.Press(GamepadButton::South);
    playground.Run(0.3f);
    playground.Release(GamepadButton::South);
    CHECK(playground.PlayerPosition().y > ground + 0.5f);
    playground.Run(1.5f);

    // The right shoulder takes the next weapon, the right trigger fires it and West reloads it.
    playground.Press(GamepadButton::RightShoulder);
    playground.Run(0.05f);
    playground.Release(GamepadButton::RightShoulder);
    REQUIRE(holder.GetWeapon() != nullptr);
    CHECK(holder.GetWeapon()->displayName == "Pistol");
    Entity& crate = playground.PlaceCrate(5.0f, 5.0f);
    playground.Run(0.05f);
    playground.Push(GamepadAxis::RightTrigger, 1.0f);
    playground.Run(0.05f);
    playground.Push(GamepadAxis::RightTrigger, 0.0f);
    CHECK(holder.GetWeapon()->GetAmmo() == 11);
    CHECK(Spot::Length(crate.GetComponent<Spot::Rigidbody>()->GetLinearVelocity()) > 4.0f);
    playground.Press(GamepadButton::West);
    playground.Run(0.05f);
    playground.Release(GamepadButton::West);
    playground.Run(2.0f);
    CHECK(holder.GetWeapon()->GetAmmo() == 12);

    // The left shoulder goes back to the physics gun.
    playground.Press(GamepadButton::LeftShoulder);
    playground.Run(0.05f);
    playground.Release(GamepadButton::LeftShoulder);
    CHECK(holder.IsPhysicsGunSelected());
}

TEST_CASE("Aiming down the sights zooms in while held, with the right button or the left trigger", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    auto& holder = *playground.player->GetComponent<WeaponHolder>();
    const Entity* head = playground.scene.GetEntity(playground.Player().head);
    REQUIRE(head != nullptr);
    const auto* camera = head->GetComponent<Spot::Camera>();
    REQUIRE(camera != nullptr);
    const float fieldOfView = camera->GetFieldOfView();

    // Not with the physics gun, which freezes with it.
    playground.Press(Spot::MouseButton::Right);
    playground.Run(0.5f);
    playground.Release(Spot::MouseButton::Right);
    CHECK_FALSE(holder.IsAiming());
    CHECK(playground.Player().GetZoom() == Approx(1.0f));

    playground.Press(Key::Digit3);
    playground.Run(0.05f);
    playground.Release(Key::Digit3);
    playground.Press(Spot::MouseButton::Right);
    playground.Run(0.5f);
    CHECK(holder.IsAiming());
    CHECK(playground.Player().GetZoom() == Approx(holder.GetWeapon()->aimZoom).margin(0.01f));
    CHECK(camera->GetFieldOfView() < fieldOfView * 0.8f);
    playground.Release(Spot::MouseButton::Right);
    playground.Run(0.5f);
    CHECK_FALSE(holder.IsAiming());
    CHECK(camera->GetFieldOfView() == Approx(fieldOfView).margin(0.01f));

    playground.Push(Spot::GamepadAxis::LeftTrigger, 1.0f);
    playground.Run(0.1f);
    CHECK(holder.IsAiming());
    playground.Push(Spot::GamepadAxis::LeftTrigger, 0.0f);
}

TEST_CASE("The playground sounds from where things are, on its buses", "[sandbox][audio]") {
    Playground playground(true);
    playground.Run(0.1f);
    // Isolate the player's footsteps from the patrolling robots' animation events.
    playground.scene.ForEachComponent<Robot>([](Robot& robot) { robot.SetEnabled(false); });
    playground.Run(1.5f);
    REQUIRE(playground.audio);

    // Music and wind play on their own buses all along, the wind streamed.
    for (const char* name : {"Music", "Ambience"}) {
        const Entity* entity = playground.scene.FindEntity(name);
        REQUIRE(entity != nullptr);
        CHECK(entity->GetComponent<Spot::AudioSource>()->IsPlaying());
        CHECK(playground.peaks[name].left > 0.01f);
    }
    CHECK(playground.scene.FindEntity("Ambience")->GetComponent<Spot::AudioSource>()->sound->IsStreamed());

    // Standing still, nothing sounds on the effects bus; walking, steps do.
    playground.Run(0.5f);
    CHECK(playground.peaks["Effects"].left == 0.0f);
    playground.Press(Key::W);
    playground.Run(1.0f);
    playground.Release(Key::W);
    CHECK(playground.peaks["Effects"].left > 0.01f);
    playground.Run(1.0f);

    // A shot is heard from the player's hands, in the middle.
    FireSlot(playground, Key::Digit2);
    const Spot::AudioPeak shot = playground.peaks["Effects"];
    CHECK(shot.left > 0.1f);
    CHECK(shot.left == Approx(shot.right).epsilon(0.1f));
    playground.Run(1.0f);

    // An explosion to the right is heard on the right.
    const Spot::Vector3 right = playground.Player().GetViewRotation() * Spot::Vector3{1.0f, 0.0f, 0.0f};
    Explosion::Explode(playground.scene, playground.Player().GetEyePosition() + right * 12.0f, 1.0f, 0.0f, 0.0f);
    playground.Run(0.3f);
    const Spot::AudioPeak blast = playground.peaks["Effects"];
    CHECK(blast.right > 0.05f);
    CHECK(blast.right > blast.left * 4.0f);
    playground.Run(2.0f);

    // A crate dropped near the player knocks like wood when it lands.
    Entity& crate = playground.PlaceCrate(4.0f, 5.0f);
    crate.GetTransform().SetPosition(crate.GetTransform().GetPosition() + Spot::Vector3{0.0f, 3.0f, 0.0f});
    crate.GetComponent<Spot::Rigidbody>()->SetGravityScale(1.0f);
    crate.AddComponent<Surface>().kind = SurfaceKind::Wood;
    crate.AddComponent<ImpactSounds>();
    playground.Run(1.5f);
    CHECK(playground.peaks["Effects"].left > 0.01f);

    // The physics gun hums while it holds something.
    playground.Press(Key::Digit1);
    playground.Run(0.05f);
    playground.Release(Key::Digit1);
    Entity& held = playground.PlaceCrate(5.0f, 5.0f);
    playground.Run(0.05f);
    playground.Press(Spot::MouseButton::Left);
    playground.Run(0.2f);
    auto& gun = *playground.player->GetComponent<PhysicsGun>();
    REQUIRE(gun.IsGrabbing());
    const Entity* hum = playground.scene.GetEntity(gun.hum);
    REQUIRE(hum != nullptr);
    CHECK(hum->GetComponent<Spot::AudioSource>()->IsPlaying());
    playground.Release(Spot::MouseButton::Left);
    playground.Run(0.1f);
    CHECK_FALSE(hum->GetComponent<Spot::AudioSource>()->IsPlaying());
    static_cast<void>(held);
}

TEST_CASE("Shots and explosions flash, and the flashes go out", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    const auto flashes = [&] {
        int count = 0;
        playground.scene.ForEachComponent<Flash>([&count](const Flash&) { ++count; });
        return count;
    };
    FireSlot(playground, Key::Digit2);
    // The muzzle flash lasts a few frames; the shot happened in the last of them.
    playground.Press(Spot::MouseButton::Left);
    playground.Run(Step);
    playground.Release(Spot::MouseButton::Left);
    playground.Run(0.3f);
    playground.Press(Spot::MouseButton::Left);
    playground.Run(Step);
    playground.Release(Spot::MouseButton::Left);
    CHECK(flashes() == 1);
    const Entity* flash = playground.scene.FindEntity("Flash");
    REQUIRE(flash != nullptr);
    CHECK(flash->GetComponent<Spot::PointLight>() != nullptr);
    playground.Run(0.2f);
    CHECK(flashes() == 0);

    Explosion::Explode(playground.scene, playground.PlayerPosition() + Spot::Vector3{0.0f, 0.0f, -10.0f}, 3.0f, 0.0f,
                       0.0f);
    CHECK(flashes() == 1);
    // The fireball's light dies down over a little longer.
    playground.Run(0.4f);
    CHECK(flashes() == 1);
    playground.Run(0.5f);
    CHECK(flashes() == 0);
}

TEST_CASE("The day cycle thickens and darkens the fog at night, and raises the exposure", "[sandbox]") {
    Playground playground;
    Entity* sun = playground.scene.FindEntity("Sun");
    REQUIRE(sun != nullptr);
    auto& cycle = *sun->GetComponent<DayCycle>();
    const auto& fog = *sun->GetComponent<Spot::Fog>();
    const auto& look = *sun->GetComponent<Spot::PostProcessing>();
    cycle.dayLength = 0.0f;

    cycle.timeOfDay = 0.5f;
    playground.Run(Step);
    const Spot::Color noonColor = fog.color;
    const float noonDensity = fog.density;
    const float noonExposure = look.exposure;

    cycle.timeOfDay = 0.0f;
    playground.Run(Step);
    CHECK(fog.color.r < noonColor.r * 0.2f);
    CHECK(fog.density > noonDensity);
    CHECK(look.exposure > noonExposure);
    CHECK(look.temperature < 0.0f);

    // At sunset the light is warm.
    cycle.timeOfDay = 0.75f;
    playground.Run(Step);
    CHECK(look.temperature > 0.1f);
    CHECK(fog.color.r > fog.color.b);
}

TEST_CASE("The player holds the model of what is in hand, in front of the eyes", "[sandbox]") {
    Playground playground;
    playground.Run(0.5f);
    const auto& view = *playground.player->GetComponent<ViewModel>();
    auto& holder = *playground.player->GetComponent<WeaponHolder>();
    const auto eye = [&] { return playground.player->GetComponent<PlayerController>()->GetEyePosition(); };

    // The physics gun is in hand first.
    REQUIRE(holder.IsPhysicsGunSelected());
    REQUIRE(view.GetShownModel());
    CHECK(view.GetShownModel().Get() == view.physicsGun.Get());
    CHECK(view.GetPartEntities().size() == view.physicsGun->GetParts().size());
    for (const Spot::EntityId id : view.GetPartEntities()) {
        const Entity* part = playground.scene.GetEntity(id);
        REQUIRE(part != nullptr);
        CHECK(Spot::Distance(part->GetTransform().GetPosition(), eye()) < 1.0f);
    }

    // Each weapon shows its own, and the tool gun its.
    playground.Press(Key::Digit2);
    playground.Run(0.05f);
    playground.Release(Key::Digit2);
    REQUIRE(holder.GetWeapon() != nullptr);
    REQUIRE(holder.GetWeapon()->model);
    CHECK(view.GetShownModel().Get() == holder.GetWeapon()->model.Get());
    playground.Press(Key::Digit6);
    playground.Run(0.05f);
    playground.Release(Key::Digit6);
    REQUIRE(holder.IsToolGunSelected());
    CHECK(view.GetShownModel().Get() == view.toolGun.Get());
}

TEST_CASE("The tool gun paints props and gives them materials, and Z takes it back", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    auto& tool = *playground.player->GetComponent<ToolGun>();
    playground.Press(Key::Digit6);
    playground.Run(0.05f);
    playground.Release(Key::Digit6);
    const auto tap = [&](auto input) {
        playground.Press(input);
        playground.Run(0.05f);
        playground.Release(input);
    };
    for (int i = 0; i < 3; ++i) {
        tap(Key::R);
    }
    REQUIRE(tool.mode == ToolGun::Mode::Color);
    // Aim picks the next color.
    tap(Spot::MouseButton::Right);
    CHECK(tool.color == 1);

    Entity& crate = playground.PlaceCrate(3.0f, 5.0f);
    REQUIRE(tool.materials.size() == 6);
    auto& renderer = crate.AddComponent<Spot::MeshRenderer>(
        playground.assets.Load<Spot::Mesh>(Spot::BuiltInAssets::CubeMesh), tool.materials[0].material);
    const Spot::AssetRef<Spot::Material> original = renderer.GetMaterial();
    REQUIRE(original);
    playground.Run(0.05f);
    tap(Spot::MouseButton::Left);
    const Spot::Color black = ToolGun::Palette[1].color;
    REQUIRE(renderer.GetMaterial());
    CHECK(renderer.GetMaterial()->baseColor == black);
    // The rest of the material stays, its textures included.
    CHECK(renderer.GetMaterial()->baseColorTexture.Get() == original->baseColorTexture.Get());

    tap(Key::R);
    REQUIRE(tool.mode == ToolGun::Mode::Material);
    for (int i = 0; i < 3; ++i) {
        tap(Spot::MouseButton::Right);
    }
    REQUIRE(tool.material == 3);
    tap(Spot::MouseButton::Left);
    const Spot::AssetRef<Spot::Material> concrete = tool.materials[3].material;
    REQUIRE(tool.materials[3].name == "Concrete");
    REQUIRE(concrete);
    CHECK(renderer.GetMaterial().Get() == concrete.Get());

    // Z takes back the material, then the paint.
    tap(Key::Z);
    CHECK(renderer.GetMaterial()->baseColor == black);
    tap(Key::Z);
    CHECK(renderer.GetMaterial().Get() == original.Get());

    // The level keeps its look.
    Entity* ground = playground.scene.FindEntity("Ground");
    REQUIRE(ground != nullptr);
    CHECK_FALSE(tool.Paint(*ground, black));
    CHECK_FALSE(tool.ApplyMaterial(*ground, concrete));
}

TEST_CASE("The tool gun places lamps of every kind, many at once, and takes them away", "[sandbox]") {
    Playground playground;
    playground.Run(1.0f);
    auto& tool = *playground.player->GetComponent<ToolGun>();
    auto& undo = *playground.player->GetComponent<UndoList>();
    Entity* ground = playground.scene.FindEntity("Ground");
    REQUIRE(ground != nullptr);
    const auto countLights = [&] {
        int count = 0;
        playground.scene.ForEachComponent<Spot::PointLight>([&count](const Spot::PointLight&) { ++count; });
        playground.scene.ForEachComponent<Spot::SpotLight>([&count](const Spot::SpotLight&) { ++count; });
        return count;
    };
    const int before = countLights();

    // Forty lamps over the ground, going through the kinds and the palette.
    for (int i = 0; i < 40; ++i) {
        tool.lamp = static_cast<ToolGun::LampKind>(i % 4);
        tool.color = i % 8;
        REQUIRE(
            tool.PlaceLamp(*ground,
                           {-15.0f + static_cast<float>(i % 8) * 4.0f, 0.0f, -15.0f + static_cast<float>(i / 8) * 4.0f},
                           {0.0f, 1.0f, 0.0f}) != nullptr);
    }
    playground.Run(0.1f);
    CHECK(countLights() == before + 40);
    CHECK(playground.scene.FindEntitiesWithTag(ToolGun::LampTag).size() == 40);
    // Spot lamps shine out of the surface, here straight up; shadowed kinds cast shadows.
    int spots = 0;
    int shadowed = 0;
    for (Entity* lamp : playground.scene.FindEntitiesWithTag(ToolGun::LampTag)) {
        CHECK(lamp->GetPrefabAssetId().IsValid());
        if (const auto* spot = lamp->GetComponent<Spot::SpotLight>()) {
            ++spots;
            CHECK(Spot::Distance(spot->GetLightData().direction, Spot::Vector3{0.0f, 1.0f, 0.0f}) < 1e-3f);
            shadowed += spot->IsCastingShadows() ? 1 : 0;
        } else if (lamp->GetComponent<Spot::PointLight>()->IsCastingShadows()) {
            ++shadowed;
        }
    }
    CHECK(spots == 20);
    CHECK(shadowed == 20);
    // Black paint gives white light, not none.
    tool.color = 1;
    CHECK(tool.GetLampColor() == Spot::Color::White);

    // A lamp on a prop moves with it.
    Entity& crate = playground.PlaceCrate(3.0f, 5.0f);
    Entity* carried = tool.PlaceLamp(crate, crate.GetTransform().GetPosition() + Spot::Vector3{0.0f, 0.25f, 0.0f},
                                     {0.0f, 1.0f, 0.0f});
    REQUIRE(carried != nullptr);
    CHECK(carried->GetParent() == &crate);

    // Undo takes back the last lamp; the remover takes the one nearest where it is clicked.
    REQUIRE(undo.Undo());
    playground.Run(0.05f);
    CHECK(playground.scene.FindEntitiesWithTag(ToolGun::LampTag).size() == 40);
    CHECK(tool.RemoveLampNear({-15.0f, 0.0f, -15.0f}));
    playground.Run(0.05f);
    CHECK(playground.scene.FindEntitiesWithTag(ToolGun::LampTag).size() == 39);
    CHECK_FALSE(tool.RemoveLampNear({-13.0f, 0.0f, -13.0f}));
    REQUIRE(undo.Undo());
    playground.Run(0.05f);
    CHECK(playground.scene.FindEntitiesWithTag(ToolGun::LampTag).size() == 40);

    tool.pointLamp = {};
    tool.lamp = ToolGun::LampKind::Point;
    CHECK(tool.PlaceLamp(*ground, {}, {}) == nullptr);
    CHECK(countLights() == before + 40);
}

TEST_CASE("Sandbox commands gate cheats, resolve scene settings and clean spawned catalog props",
          "[sandbox][console]") {
    Playground game;
    game.Run(Step);
    const auto previousScale = Spot::Time::GetTimeScale();
    struct RestoreScale {
        float value;
        ~RestoreScale() { Spot::Time::SetTimeScale(value); }
    } restore{previousScale};
    auto* spawner = game.player->GetComponent<PropSpawner>();
    auto* movement = game.player->GetComponent<PlayerMovement>();
    REQUIRE(spawner);
    REQUIRE(movement);
    REQUIRE_FALSE(spawner->GetSpawnables().empty());
    const auto bodies = game.scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount();
    Spot::Console console;
    {
        SandboxCommands commands(console, game.scene);
        CHECK_FALSE(console.Execute("spawn crate"));
        CHECK(spawner->GetSpawnedCount() == 0);
        console.SetCheatsEnabled(true);
        game.scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({1.0f, -9.81f, 2.0f});
        REQUIRE(console.Execute("gravity 0"));
        CHECK(game.scene.GetOrAddSystem<Spot::PhysicsWorld>().GetGravity() == Spot::Vector3{1.0f, 0.0f, 2.0f});
        CHECK_FALSE(console.Execute("gravity nan"));
        CHECK_FALSE(console.Execute("gravity 1001"));
        REQUIRE(console.Execute("timescale 0.25"));
        CHECK(Spot::Time::GetTimeScale() == 0.25f);
        CHECK_FALSE(console.Execute("timescale -1"));
        CHECK_FALSE(console.Execute("timescale inf"));
        CHECK(Spot::Time::GetTimeScale() == 0.25f);
        REQUIRE(console.Execute("noclip true"));
        CHECK(movement->IsFlying());
        REQUIRE(console.Execute("noclip"));
        CHECK_FALSE(movement->IsFlying());
        CHECK_FALSE(console.Execute("noclip maybe"));
        REQUIRE(console.Execute("spawn crate"));
        CHECK(spawner->GetSpawnedCount() == 1);
        CHECK_FALSE(console.Execute("spawn unknown"));
        CHECK_FALSE(console.Execute("cleanup extra"));
        CHECK(spawner->GetSpawnedCount() == 1);
        REQUIRE(console.Execute("cleanup"));
        game.Run(Step);
        CHECK(spawner->GetSpawnedCount() == 0);
        CHECK(game.scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == bodies);
        CHECK_FALSE(console.Execute("give crate"));
    }
    CHECK(console.Complete("spawn").empty());
    CHECK(console.Complete("stress").empty());
}

TEST_CASE("Giving weapons refills existing inventory and creates missing held weapons without world bodies",
          "[sandbox][console]") {
    Playground game;
    game.Run(Step);
    auto* holder = game.player->GetComponent<WeaponHolder>();
    REQUIRE(holder);
    Spot::Console console;
    SandboxCommands commands(console, game.scene);
    console.SetCheatsEnabled(true);
    const auto original = holder->GetWeapons();
    REQUIRE_FALSE(original.empty());
    auto* pistol = original.front();
    const auto name = pistol->displayName;
    REQUIRE(pistol->TakeShot());
    REQUIRE(pistol->StartReload());
    REQUIRE(console.Execute("give \"" + name + "\""));
    CHECK(holder->GetWeapon() == pistol);
    CHECK(holder->GetWeapons().size() == original.size());
    CHECK(pistol->GetAmmo() == pistol->magazineSize);
    CHECK(pistol->GetReserve() == pistol->reserveAmmo);
    CHECK_FALSE(pistol->IsReloading());
    CHECK(pistol->IsReady());
    const auto removedId = pistol->GetEntity().GetId();
    pistol->GetEntity().Destroy();
    game.Run(Step);
    const auto bodies = game.scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount();
    REQUIRE(console.Execute("give \"" + name + "\""));
    auto* given = holder->GetWeapon();
    REQUIRE(given);
    CHECK(given->GetEntity().GetId() != removedId);
    CHECK(given->GetEntity().GetParent() == game.player);
    CHECK_FALSE(given->GetEntity().HasComponent<Spawnable>());
    CHECK_FALSE(given->GetEntity().HasComponent<Spot::Rigidbody>());
    CHECK_FALSE(given->GetEntity().HasComponent<Spot::Collider>());
    CHECK_FALSE(given->GetEntity().HasComponent<Spot::MeshRenderer>());
    game.Run(Step);
    CHECK(holder->GetWeapons().size() == original.size());
    CHECK(given->GetAmmo() == given->magazineSize);
    CHECK(game.scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == bodies);
}

TEST_CASE("Sandbox stress piles record physics scopes and cleanup removes their live bodies",
          "[sandbox][console][profiler]") {
    Playground game;
    game.Run(Step);
    auto* spawner = game.player->GetComponent<PropSpawner>();
    REQUIRE(spawner);
    Spot::Console console;
    SandboxCommands commands(console, game.scene);
    console.SetCheatsEnabled(true);
    for (const auto* invalid : {"stress", "stress -1", "stress 0", "stress 2001", "stress 1.5", "stress 4 extra"}) {
        CHECK_FALSE(console.Execute(invalid));
    }
    CHECK(spawner->GetSpawnedCount() == 0);
    const auto bodies = game.scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount();
    REQUIRE(console.Execute("stress 16"));
    CHECK(spawner->GetSpawnedCount() == 16);
    Spot::Profiler::Clear();
    Spot::Profiler::BeginFrame();
    game.Run(Step * 2.0f);
    const auto stressed = game.scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount();
    Spot::Profiler::Count("Bodies", static_cast<double>(stressed));
    Spot::Profiler::EndFrame();
    const auto frames = Spot::Profiler::GetFrames();
    REQUIRE(frames.size() == 1);
    CHECK(stressed == bodies + 16);
    CHECK(Spot::Profiler::GetCounter(*frames.back(), "Bodies") == static_cast<double>(bodies + 16));
    CHECK(std::ranges::any_of(frames.back()->samples, [](const auto& sample) {
        return sample.name.find("Physics") != std::string_view::npos;
    }));
    REQUIRE(console.Execute("cleanup"));
    game.Run(Step);
    CHECK(spawner->GetSpawnedCount() == 0);
    CHECK(game.scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == bodies);
    Spot::Profiler::Clear();
}

TEST_CASE("Sandbox console registrations follow GameControls enable and scene lifetime", "[sandbox][console]") {
    {
        Playground game;
        game.Run(Step);
        auto* controls = game.scene.FindComponent<GameControls>();
        REQUIRE(controls);
        CHECK_FALSE(Spot::Console::Get().Complete("stress").empty());
        controls->SetEnabled(false);
        CHECK(Spot::Console::Get().Complete("stress").empty());
        controls->SetEnabled(true);
        CHECK_FALSE(Spot::Console::Get().Complete("stress").empty());
    }
    CHECK(Spot::Console::Get().Complete("stress").empty());
}

namespace {
std::size_t ParticleCount(Spot::Scene& scene, Spot::Color color) {
    std::size_t count = 0;
    scene.ForEachComponent<Spot::ParticleSystem>([&](Spot::ParticleSystem& effect) {
        if (effect.GetSettings().startColor == color)
            count += effect.GetParticles().size();
    });
    return count;
}
} // namespace
TEST_CASE("Sandbox weapons emit muzzle particles and metal impact sparks", "[sandbox][particles]") {
    Playground playground;
    playground.Run(1.5f);
    auto* bank = ParticleEffects::Find(playground.scene);
    REQUIRE(bank);
    auto& crate = playground.PlaceCrate(5, 5);
    crate.AddComponent<Surface>().kind = SurfaceKind::Metal;
    playground.Run(0.05f);
    FireSlot(playground, Key::Digit2);
    CHECK(ParticleCount(playground.scene, bank->muzzleSmoke.settings.startColor) > 0);
    CHECK(ParticleCount(playground.scene, bank->sparks.settings.startColor) > 0);
    auto* pool = playground.scene.GetSystem<ParticleEffectPool>();
    REQUIRE(pool);
    CHECK(pool->GetEmitterCount() <= ParticleEffectPool::MaxEmitters);
    playground.Run(6);
    CHECK(ParticleCount(playground.scene, bank->muzzleSmoke.settings.startColor) == 0);
    CHECK(ParticleCount(playground.scene, bank->sparks.settings.startColor) == 0);
}
TEST_CASE("Sandbox surface impacts and explosions select authored particle definitions", "[sandbox][particles]") {
    Playground playground;
    playground.Run(1.5f);
    auto* bank = ParticleEffects::Find(playground.scene);
    REQUIRE(bank);
    for (auto surface : {SurfaceKind::Concrete, SurfaceKind::Wood, SurfaceKind::Metal})
        ParticleEffects::Impact(playground.scene, {1000, 1000, 1000}, {0, 1, 0}, surface);
    CHECK(ParticleCount(playground.scene, bank->concreteDust.settings.startColor) == bank->concreteDust.count);
    CHECK(ParticleCount(playground.scene, bank->woodDust.settings.startColor) == bank->woodDust.count);
    CHECK(ParticleCount(playground.scene, bank->sparks.settings.startColor) == bank->sparks.count);
    Explosion::Explode(playground.scene, {1000, 1000, 1000}, 5, 100, 0);
    // An explosion in the air unfolds in layers, but raises no dust.
    for (const ParticleEffect* effect : {&bank->blastFlash, &bank->fireball, &bank->explosionSmoke, &bank->embers})
        CHECK(ParticleCount(playground.scene, effect->settings.startColor) == effect->count);
    CHECK(ParticleCount(playground.scene, bank->explosionDebris.settings.startColor) >= bank->explosionDebris.count);
    CHECK(ParticleCount(playground.scene, bank->groundDust.settings.startColor) == 0);
    playground.Run(6);
    for (const ParticleEffect* effect :
         {&bank->blastFlash, &bank->fireball, &bank->explosionSmoke, &bank->embers, &bank->explosionDebris})
        CHECK(ParticleCount(playground.scene, effect->settings.startColor) == 0);
    // Close above the ground, it kicks dust up from it.
    const Spot::Vector3 above = playground.PlayerPosition() + Spot::Vector3{0, 20, -10};
    const auto ground = playground.scene.GetOrAddSystem<Spot::PhysicsWorld>().Raycast(above, {0, -1, 0}, 40);
    REQUIRE(ground);
    Explosion::Explode(playground.scene, ground->point + Spot::Vector3{0, 0.5f, 0}, 5, 0, 0);
    CHECK(ParticleCount(playground.scene, bank->groundDust.settings.startColor) == bank->groundDust.count);
}
TEST_CASE("Sandbox effect pooling bounds bursts and reuses expired emitters", "[sandbox][particles]") {
    Spot::Scene scene;
    auto* bank = &scene.CreateEntity().AddComponent<ParticleEffects>();
    auto& pool = scene.AddSystem<ParticleEffectPool>();
    for (std::size_t i = 0; i < ParticleEffectPool::MaxEmitters + 10; ++i)
        pool.Spawn(bank->muzzleFlash, {1000, 1000, 1000});
    CHECK(pool.GetEmitterCount() == ParticleEffectPool::MaxEmitters);
    CHECK(pool.GetDroppedCount() == 10);
    scene.Update(0.5f);
    const auto entities = scene.GetEntityCount();
    REQUIRE(pool.Spawn(bank->muzzleFlash, {1000, 1000, 1000}));
    CHECK(scene.GetEntityCount() == entities);
    const auto id = pool.Spawn(bank->muzzleFlash, {1000, 1000, 1000})->GetEntity().GetId();
    scene.GetEntity(id)->Destroy();
    REQUIRE(pool.Spawn(bank->muzzleFlash, {1000, 1000, 1000}));
    CHECK(pool.GetEmitterCount() == ParticleEffectPool::MaxEmitters - 1);
}
TEST_CASE("The held physics gun emits beam particles and stops emission on release", "[sandbox][particles]") {
    Playground playground;
    playground.Run(1.5f);
    auto* bank = ParticleEffects::Find(playground.scene);
    REQUIRE(bank);
    playground.PlaceCrate(5, 5);
    playground.Run(0.05f);
    playground.Press(Key::Digit1);
    playground.Run(0.05f);
    playground.Release(Key::Digit1);
    playground.Press(Spot::MouseButton::Left);
    playground.Run(0.1f);
    REQUIRE(playground.player->GetComponent<PhysicsGun>()->IsGrabbing());
    CHECK(ParticleCount(playground.scene, bank->beam.settings.startColor) > 0);
    playground.Release(Spot::MouseButton::Left);
    playground.Run(0.4f);
    CHECK(ParticleCount(playground.scene, bank->beam.settings.startColor) == 0);
    float firstRemainder = 0, secondRemainder = 0;
    // Equal elapsed time produces equal counts at different render update rates.
    Spot::Scene first, second;
    auto& firstBank = first.CreateEntity().AddComponent<ParticleEffects>();
    auto& secondBank = second.CreateEntity().AddComponent<ParticleEffects>();
    for (int i = 0; i < 10; ++i)
        ParticleEffects::Beam(first, {0, 0, 0}, {0, 0, -3}, 0.001f, firstRemainder);
    for (int i = 0; i < 2; ++i)
        ParticleEffects::Beam(second, {0, 0, 0}, {0, 0, -3}, 0.005f, secondRemainder);
    CHECK(ParticleCount(first, firstBank.beam.settings.startColor) ==
          ParticleCount(second, secondBank.beam.settings.startColor));
}

TEST_CASE("Prop collisions emit surface particles without an audio device", "[sandbox][particles]") {
    Playground playground;
    playground.Run(1.5f);
    auto* bank = ParticleEffects::Find(playground.scene);
    REQUIRE(bank);
    auto& ground = playground.scene.CreateEntity("Particle Test Ground");
    ground.GetTransform().SetPosition({1000, -0.5f, 1000});
    ground.AddComponent<Spot::BoxCollider>(Spot::Vector3{5, 0.5f, 5});
    auto& prop = playground.scene.CreateEntity("Particle Test Prop");
    prop.GetTransform().SetPosition({1000, 3, 1000});
    prop.AddComponent<Spot::BoxCollider>(Spot::Vector3{0.5f, 0.5f, 0.5f});
    prop.AddComponent<Spot::Rigidbody>();
    prop.AddComponent<Surface>().kind = SurfaceKind::Metal;
    prop.AddComponent<ImpactParticles>();
    playground.Run(0.85f);
    CHECK(ParticleCount(playground.scene, bank->sparks.settings.startColor) > 0);
}
