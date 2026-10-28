#include "gameplay_world.h"

#include "gameplay/core/controller.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/environment/water.h"
#include "gameplay/environment/wind_sway.h"
#include "gameplay/vehicles/helicopter.h"
#include "gameplay/vehicles/helicopter_downwash.h"

#include <spot_tools.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

namespace {

class Pilot final : public Gameplay::Controller {};

Gameplay::Helicopter& MakeHelicopter(Spot::Scene& scene) {
    auto& entity = scene.CreateEntity("Helicopter");
    entity.GetTransform().SetPosition({0.0f, 0.03f, 0.0f});
    auto& box = entity.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.8f, 1.8f, 4.0f});
    box.SetCenter({0.0f, 1.2f, 0.0f});
    auto& body = entity.AddComponent<Spot::Rigidbody>();
    body.SetMass(900.0f);
    body.SetContinuous(true);
    auto& helicopter = entity.AddComponent<Gameplay::Helicopter>();
    auto& main = entity.CreateChild("Main Rotor");
    main.GetTransform().SetPosition({0.0f, 3.25f, 0.0f});
    helicopter.mainRotor = main.GetId();
    auto& tail = entity.CreateChild("Tail Rotor");
    tail.GetTransform().SetPosition({0.28f, 2.1f, 5.9f});
    helicopter.tailRotor = tail.GetId();
    return helicopter;
}

} // namespace

TEST_CASE("Helicopter downwash makes bounded water ripples that fade with height and rotor speed",
          "[sandbox][gameplay][vehicles][helicopter][water]") {
    const float height = GENERATE(2.0f, 12.0f);
    CAPTURE(height);
    GameplayTests::World world;
    auto& pond = world.scene.CreateEntity("Pond");
    pond.GetTransform().SetScale({40.0f, 1.0f, 40.0f});
    auto& water = pond.AddComponent<Gameplay::Water>();
    Gameplay::ParticleLayer ring;
    ring.settings.facing = Spot::ParticleFacing::Horizontal;
    ring.settings.speed = {0.0f, 0.0f};
    ring.settings.gravity = {};
    ring.settings.lifetime = {0.5f, 0.5f};
    world.scene.CreateEntity("Effects").AddComponent<Gameplay::EffectLibrary>().effects = {
        {.name = "splash.ripples", .layers = {ring}}};
    auto& helicopter = MakeHelicopter(world.scene);
    auto& entity = helicopter.GetEntity();
    entity.GetTransform().SetPosition({0.0f, height, 0.0f});
    entity.GetComponent<Spot::Rigidbody>()->SetGravityScale(0.0f);
    auto& wash = entity.AddComponent<Gameplay::HelicopterDownwash>();
    world.Run(0.3f);
    CHECK_FALSE(world.scene.FindComponent<Spot::ParticleSystem>());

    auto& pilot = world.scene.CreateEntity("Pilot").AddComponent<Pilot>();
    pilot.Possess(helicopter);
    helicopter.spoolTime = 1.0f;
    wash.SetEnabled(false);
    world.Run(0.5f);
    wash.SetEnabled(true);
    world.Step(0.0f);
    CHECK_FALSE(world.scene.FindComponent<Spot::ParticleSystem>());
    world.Step();
    auto* emitter = world.scene.FindComponent<Spot::ParticleSystem>();
    REQUIRE(emitter);
    REQUIRE(emitter->GetParticles().size() == 1);
    const auto& particle = emitter->GetParticles().front();
    CHECK(particle.position.y == Catch::Approx(water.GetHeight() + 0.025f));
    CHECK(particle.position.x == Catch::Approx(0.0f).margin(0.01f));
    CHECK(particle.position.z == Catch::Approx(0.0f).margin(0.01f));
    const float falloff = 1.0f - (height + 3.25f) / wash.range;
    const float speed = helicopter.GetRotorSpeed();
    CHECK(speed < 1.0f);
    CHECK(emitter->GetSettings().startColor.a == Catch::Approx(speed * speed * falloff * falloff));
    CHECK(emitter->GetSettings().facing == Spot::ParticleFacing::Horizontal);
    world.Run(2.0f);
    CHECK(Gameplay::EffectPool::Get(world.scene).GetEmitterCount() <= 4);
    CHECK(Gameplay::EffectPool::Get(world.scene).GetDroppedCount() == 0);

    entity.GetTransform().SetPosition({0.0f, 30.0f, 0.0f});
    world.Run(1.0f);
    world.scene.ForEachComponent<Spot::ParticleSystem>(
        [](Spot::ParticleSystem& system) { CHECK(system.GetParticles().empty()); });
    entity.GetTransform().SetPosition({60.0f, height, 0.0f});
    world.Run(1.0f);
    world.scene.ForEachComponent<Spot::ParticleSystem>(
        [](Spot::ParticleSystem& system) { CHECK(system.GetParticles().empty()); });
    // Tilt sends the wash to the water along the rotor axis rather than straight below the body.
    entity.GetTransform().SetPosition({0.0f, height, 0.0f});
    entity.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, 0.2f));
    world.Run(0.3f);
    emitter = world.scene.FindComponent<Spot::ParticleSystem>();
    REQUIRE(emitter);
    REQUIRE_FALSE(emitter->GetParticles().empty());
    CHECK(emitter->GetParticles().front().position.x > 0.1f);

    pilot.Unpossess();
    world.Run(1.6f);
    CHECK(helicopter.GetRotorSpeed() == 0.0f);
    world.scene.ForEachComponent<Spot::ParticleSystem>(
        [](Spot::ParticleSystem& system) { CHECK(system.GetParticles().empty()); });
}

TEST_CASE("A helicopter climbs hovers pitches turns descends and shuts down through Vehicle",
          "[sandbox][gameplay][vehicles][helicopter]") {
    GameplayTests::World world;
    auto& helicopter = MakeHelicopter(world.scene);
    auto& entity = helicopter.GetEntity();
    auto& body = *entity.GetComponent<Spot::Rigidbody>();
    world.Run(1.0f);
    CHECK(helicopter.GetRotorSpeed() == 0.0f);
    CHECK(std::abs(body.GetLinearVelocity().y) < 0.1f);

    auto& pilot = world.scene.CreateEntity("Pilot").AddComponent<Pilot>();
    pilot.Possess(helicopter);
    helicopter.SetInput({.lift = 1.0f});
    world.Run(6.0f);
    CHECK(helicopter.GetRotorSpeed() == Catch::Approx(1.0f));
    CHECK(entity.GetTransform().GetPosition().y > 5.0f);
    CHECK(body.GetLinearVelocity().y > 2.0f);
    const auto* rotor = world.scene.GetEntity(helicopter.mainRotor);
    REQUIRE(rotor);
    CHECK(rotor->GetTransform().GetPosition().y == Catch::Approx(3.25f));

    helicopter.SetInput({});
    world.Run(2.0f);
    const float height = entity.GetTransform().GetPosition().y;
    world.Run(1.0f);
    CHECK(std::abs(entity.GetTransform().GetPosition().y - height) < 0.25f);

    helicopter.SetInput({.drive = 1.0f});
    world.Run(3.0f);
    CHECK(helicopter.GetForwardSpeed() > 1.0f);
    CHECK(entity.GetTransform().GetPosition().z < -1.0f);

    helicopter.SetInput({.steer = 1.0f});
    world.Run(1.0f);
    CHECK(body.GetAngularVelocity().y < -0.1f);
    helicopter.SetInput({.lift = -1.0f});
    world.Run(1.0f);
    CHECK(body.GetLinearVelocity().y < -1.0f);

    pilot.Unpossess();
    world.Run(4.0f);
    CHECK(helicopter.GetRotorSpeed() == 0.0f);
    CHECK_FALSE(helicopter.HasDriver());
    CHECK(helicopter.GetInput().lift == 0.0f);
}

TEST_CASE("Aircraft vertical controls route through the shared vehicle input context",
          "[sandbox][gameplay][vehicles][helicopter]") {
    GameplayTests::World world;
    const auto project = Spot::Project::Load(std::filesystem::path(SANDBOX_ASSETS_DIR).parent_path() / "Sandbox.spot");
    REQUIRE(project);
    project->GetSettings().sections.input.Apply(world.actions);
    auto& helicopter = MakeHelicopter(world.scene);
    auto& player = world.scene.CreateEntity("Player").AddComponent<Gameplay::PlayerController>();
    player.pawn = helicopter.GetEntity().GetId();
    world.Step();
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Space, false});
    world.Step();
    CHECK(helicopter.GetInput().lift == 1.0f);
    CHECK_FALSE(helicopter.GetInput().handbrake);
    world.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::Space});
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::LeftControl, false});
    world.Step();
    CHECK(helicopter.GetInput().lift == -1.0f);
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Space, false});
    world.Step();
    CHECK(helicopter.GetInput().lift == 0.0f);
    CHECK(helicopter.DescribeTelemetry().size() == 3);
    CHECK(helicopter.DescribeHints().size() == 5);
    CHECK(Gameplay::InputPrompts::GetKeyName(helicopter.controls.accelerate) == "W/S");
    CHECK(Gameplay::InputPrompts::GetKeyName(helicopter.controls.steer) == "D/A");

    world.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::Space});
    world.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::LeftControl});
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::A, false});
    world.Step();
    CHECK(helicopter.GetInput().drive == 1.0f);
    CHECK(helicopter.GetInput().steer == -1.0f);
    CHECK(helicopter.GetInput().strafe == 0.0f);
    CHECK(helicopter.GetInput().lift == 0.0f);

    const Gameplay::VehicleControls groundControls;
    CHECK(groundControls.GetActions().size() == 4);
}

TEST_CASE("Helicopter gamepad axes independently control lift tilt and yaw",
          "[sandbox][gameplay][vehicles][helicopter][gamepad]") {
    GameplayTests::World world;
    const auto project = Spot::Project::Load(std::filesystem::path(SANDBOX_ASSETS_DIR).parent_path() / "Sandbox.spot");
    REQUIRE(project);
    project->GetSettings().sections.input.Apply(world.actions);
    auto& helicopter = MakeHelicopter(world.scene);
    auto& player = world.scene.CreateEntity("Player").AddComponent<Gameplay::PlayerController>();
    player.pawn = helicopter.GetEntity().GetId();
    constexpr auto Gamepad = static_cast<Spot::GamepadId>(1);
    const auto push = [&](Spot::GamepadAxis axis, float value) {
        world.input.OnEvent(Spot::GamepadAxisEvent{.gamepad = Gamepad, .axis = axis, .value = value});
        world.Step();
    };
    world.Step();

    push(Spot::GamepadAxis::RightTrigger, 0.65f);
    CHECK(helicopter.GetInput().lift == Catch::Approx(0.65f));
    CHECK(helicopter.GetInput().drive == 0.0f);
    push(Spot::GamepadAxis::LeftTrigger, 0.25f);
    CHECK(helicopter.GetInput().lift == Catch::Approx(0.4f));
    push(Spot::GamepadAxis::RightTrigger, 0.0f);
    CHECK(helicopter.GetInput().lift == Catch::Approx(-0.25f));
    push(Spot::GamepadAxis::LeftTrigger, 0.0f);

    push(Spot::GamepadAxis::LeftY, 0.6f);
    CHECK(helicopter.GetInput().drive == Catch::Approx(0.5f));
    CHECK(helicopter.GetInput().lift == 0.0f);
    push(Spot::GamepadAxis::LeftX, -0.6f);
    CHECK(helicopter.GetInput().strafe == Catch::Approx(-0.5f));
    CHECK(helicopter.GetInput().steer == 0.0f);
    push(Spot::GamepadAxis::LeftY, -1.0f);
    push(Spot::GamepadAxis::LeftX, 1.0f);
    CHECK(helicopter.GetInput().drive == -1.0f);
    CHECK(helicopter.GetInput().strafe == 1.0f);

    world.input.OnEvent(
        Spot::GamepadButtonPressedEvent{.gamepad = Gamepad, .button = Spot::GamepadButton::LeftShoulder});
    world.Step();
    CHECK(helicopter.GetInput().steer == -1.0f);
    world.input.OnEvent(
        Spot::GamepadButtonPressedEvent{.gamepad = Gamepad, .button = Spot::GamepadButton::RightShoulder});
    world.Step();
    CHECK(helicopter.GetInput().steer == 0.0f);
    world.input.OnEvent(
        Spot::GamepadButtonReleasedEvent{.gamepad = Gamepad, .button = Spot::GamepadButton::LeftShoulder});
    world.Step();
    CHECK(helicopter.GetInput().steer == 1.0f);
    CHECK(helicopter.GetInput().drive == -1.0f);
    CHECK(helicopter.GetInput().strafe == 1.0f);

    world.input.OnEvent(
        Spot::GamepadButtonReleasedEvent{.gamepad = Gamepad, .button = Spot::GamepadButton::RightShoulder});
    push(Spot::GamepadAxis::LeftX, 0.1f);
    push(Spot::GamepadAxis::LeftY, -0.1f);
    CHECK(helicopter.GetInput().drive == 0.0f);
    CHECK(helicopter.GetInput().strafe == 0.0f);
    CHECK(helicopter.GetInput().steer == 0.0f);
    CHECK(helicopter.GetInput().lift == 0.0f);
    world.input.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = Gamepad, .button = Spot::GamepadButton::South});
    world.input.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = Gamepad, .button = Spot::GamepadButton::East});
    world.Step();
    CHECK(helicopter.GetInput().lift == 0.0f);
    CHECK_FALSE(helicopter.GetInput().handbrake);

    const Gameplay::VehicleControls groundControls;
    CHECK(world.actions.GetValue(groundControls.accelerate) == 0.0f);
    push(Spot::GamepadAxis::RightTrigger, 0.75f);
    CHECK(world.actions.GetValue(groundControls.accelerate) == Catch::Approx(0.75f));
    CHECK(world.actions.GetValue(groundControls.brake) == 0.0f);
}

TEST_CASE("Helicopter banking follows its heading limits diagonal tilt and levels on release",
          "[sandbox][gameplay][vehicles][helicopter]") {
    const float bank = GENERATE(-1.0f, 1.0f);
    const float pitch = GENERATE(-1.0f, 0.0f, 1.0f);
    const float yaw = GENERATE(0.0f, Spot::Pi * 0.5f);
    CAPTURE(bank, pitch, yaw);
    GameplayTests::World world;
    auto& helicopter = MakeHelicopter(world.scene);
    auto& entity = helicopter.GetEntity();
    auto& body = *entity.GetComponent<Spot::Rigidbody>();
    const auto rotation = Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
    entity.GetTransform().SetRotation(rotation);
    auto& pilot = world.scene.CreateEntity("Pilot").AddComponent<Pilot>();
    pilot.Possess(helicopter);
    helicopter.SetInput({.lift = 1.0f});
    world.Run(6.0f);
    helicopter.SetInput({.drive = pitch, .strafe = bank});
    world.Run(3.0f);
    const auto velocity = body.GetLinearVelocity();
    const auto right = rotation * Spot::Vector3{1.0f, 0.0f, 0.0f};
    const auto forward = rotation * Spot::Vector3{0.0f, 0.0f, -1.0f};
    CHECK(Spot::Dot(velocity, right) * bank > 1.0f);
    if (pitch != 0.0f) {
        CHECK(Spot::Dot(velocity, forward) * pitch > 1.0f);
    }
    const auto up = entity.GetTransform().GetWorldRotation() * Spot::Vector3{0.0f, 1.0f, 0.0f};
    CHECK(up.y >= std::cos(helicopter.maxTilt + 0.02f));
    CHECK(up.y < 0.99f);
    CHECK(std::abs(body.GetLinearVelocity().y) < 0.3f);

    helicopter.SetInput({});
    world.Run(3.0f);
    const auto leveledUp = entity.GetTransform().GetWorldRotation() * Spot::Vector3{0.0f, 1.0f, 0.0f};
    CHECK(leveledUp.y > 0.999f);
    CHECK(std::abs(body.GetLinearVelocity().y) < 0.3f);
}

TEST_CASE("Rotor wind sways nearby trees and lets them settle", "[sandbox][gameplay][helicopter][wind]") {
    GameplayTests::World world;
    auto& tree = world.scene.CreateEntity("Tree");
    tree.GetTransform().SetPosition({3.0f, 0.0f, 0.0f});
    auto rig = std::make_shared<Spot::Skeleton>();
    rig->bones = {{"Trunk", -1, {}}, {"BranchA", 0, {}}, {"BranchB", 0, {}}};
    rig->joints = {0, 1, 2};
    rig->inverseBindMatrices.resize(3);
    tree.AddComponent<Gameplay::WindSway>().skeleton = rig;
    auto& crown = tree.CreateChild("Crown");
    crown.AddComponent<Spot::MeshRenderer>();
    auto& helicopter = MakeHelicopter(world.scene);
    helicopter.GetEntity().GetTransform().SetPosition({0.0f, 5.0f, 0.0f});
    helicopter.GetEntity().GetComponent<Spot::Rigidbody>()->SetGravityScale(0.0f);
    auto& wash = helicopter.GetEntity().AddComponent<Gameplay::HelicopterDownwash>();
    world.Run(0.3f);
    CHECK(crown.GetTransform().GetRotation() == Spot::Quaternion::Identity());
    world.scene.CreateEntity("Pilot").AddComponent<Pilot>().Possess(helicopter);
    world.Run(4.0f);
    auto* skin = crown.GetComponent<Spot::SkinnedMeshRenderer>();
    REQUIRE(skin);
    CHECK(skin->IsEnabled());
    REQUIRE(skin->GetPalette().size() == 3);
    const auto a = skin->GetPalette()[1] * Spot::Vector4{0, 1, 0, 0};
    const auto b = skin->GetPalette()[2] * Spot::Vector4{0, 1, 0, 0};
    CHECK(std::abs(a.x) + std::abs(a.z) > 0.05f);
    CHECK(std::abs(a.x - b.x) + std::abs(a.z - b.z) > 0.01f);
    CHECK(tree.GetTransform().GetRotation() == Spot::Quaternion::Identity());
    wash.SetEnabled(false);
    world.Run(3.0f);
    CHECK_FALSE(skin->IsEnabled());
    CHECK(crown.GetComponent<Spot::MeshRenderer>()->IsEnabled());
}
