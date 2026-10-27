#include "gameplay_world.h"

#include "gameplay/core/controller.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/vehicles/helicopter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    world.actions.Add("Jump", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::Space));
    world.actions.Add("Crouch", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::LeftControl));
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
    CHECK(helicopter.DescribeHints().size() == 3);

    const Gameplay::VehicleControls groundControls;
    CHECK(groundControls.GetActions().size() == 4);
}
