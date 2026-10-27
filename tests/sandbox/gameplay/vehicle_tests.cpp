#include "gameplay_world.h"
#include "temp_directory.h"

#include "gameplay/camera/chase_camera.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/health/damage.h"
#include "gameplay/health/health.h"
#include "gameplay/hud/hud.h"
#include "gameplay/persistence/world_save.h"
#include "gameplay/vehicles/vehicle_elements.h"
#include "gameplay/vehicles/vehicle_seat.h"
#include "gameplay/vehicles/vehicle_wheel.h"
#include "gameplay/vehicles/wheeled_vehicle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

namespace {

class Driver final : public Gameplay::Controller {};

// A vehicle of the game's own, with no wheels or drivetrain. Only its input, speed and telemetry differ.
class TestVehicle final : public Gameplay::Vehicle {
public:
    float GetForwardSpeed() const override { return 3.0f; }
    std::vector<Gameplay::Gauge> DescribeTelemetry() const override { return {{"Speed", "3", "m/s"}}; }
};

Gameplay::WheeledVehicle& MakeWheeled(Spot::Scene& scene, int count) {
    auto& entity = scene.CreateEntity("Vehicle");
    entity.GetTransform().SetPosition({0.0f, 0.02f, 0.0f});
    auto& box = entity.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.7f, 0.6f, 4.2f});
    box.SetCenter({0.0f, 0.65f, 0.0f});
    auto& body = entity.AddComponent<Spot::Rigidbody>();
    body.SetMass(1200.0f);
    body.SetLinearDamping(0.0f);
    auto& vehicle = entity.AddComponent<Gameplay::WheeledVehicle>();
    const std::vector<Spot::Vector3> positions =
        count == 3 ? std::vector<Spot::Vector3>{{0.0f, 0.33f, -1.3f}, {-0.78f, 0.33f, 1.3f}, {0.78f, 0.33f, 1.3f}}
                   : std::vector<Spot::Vector3>{{-0.78f, 0.33f, -1.3f}, {0.78f, 0.33f, -1.3f}, {-0.78f, 0.33f, 0.0f},
                                                {0.78f, 0.33f, 0.0f},   {-0.78f, 0.33f, 1.3f}, {0.78f, 0.33f, 1.3f}};
    for (const auto& position : positions) {
        auto& child = entity.CreateChild("Wheel");
        child.GetTransform().SetPosition(position);
        auto& wheel = child.AddComponent<Gameplay::VehicleWheel>();
        const bool front = position.z < 0.0f;
        wheel.maxSteer = front ? Spot::ToRadians(34.0f) : 0.0f;
        wheel.driveShare = front ? 0.0f : 1.0f;
        wheel.brakeShare = 1.0f / static_cast<float>(count);
        wheel.handbrake = !front;
        wheel.axle = position.z < 0.0f ? 0 : position.z == 0.0f ? 1 : 2;
    }
    return vehicle;
}

} // namespace

TEST_CASE("Three and six wheel vehicles rest and drive from the same simulation", "[sandbox][gameplay][vehicles]") {
    const int count = GENERATE(3, 6);
    CAPTURE(count);
    GameplayTests::World world;
    auto& vehicle = MakeWheeled(world.scene, count);
    world.Run(2.0f);
    REQUIRE(vehicle.GetWheels().size() == static_cast<std::size_t>(count));
    CHECK(std::abs(vehicle.GetEntity().GetTransform().GetWorldPosition().y) < 0.08f);
    for (const auto* wheel : vehicle.GetWheels()) {
        CHECK(wheel->IsGrounded());
        CHECK(wheel->GetLoad() > 0.0f);
    }
    auto& driver = world.scene.CreateEntity("Driver").AddComponent<Driver>();
    driver.Possess(vehicle);
    vehicle.SetInput({.drive = 1.0f});
    world.Run(2.0f);
    CHECK(vehicle.GetForwardSpeed() > 3.0f);
    CHECK(vehicle.GetEntity().GetTransform().GetWorldPosition().z < -2.0f);
    CHECK(std::abs(vehicle.GetEntity().GetTransform().GetWorldPosition().x) < 0.2f);
}

TEST_CASE("A new vehicle type uses the existing seat camera HUD damage and save", "[sandbox][gameplay][vehicles]") {
    GameplayTests::World world;
    Spot::RegisterComponent<TestVehicle>("GameplayTestVehicle");
    auto& walker = world.scene.CreateEntity("Walker");
    walker.GetTransform().SetPosition({4.0f, 1.0f, 0.0f});
    auto& pawn = walker.AddComponent<Gameplay::Pawn>();
    auto& eye = walker.CreateChild("Eye");
    eye.AddComponent<Spot::Camera>();
    pawn.viewCamera = eye.GetId();
    auto& player = world.scene.CreateEntity("Player").AddComponent<Gameplay::PlayerController>();
    player.pawn = walker.GetId();
    auto& entity = world.scene.CreateEntity("Other Vehicle");
    auto& vehicle = entity.AddComponent<TestVehicle>();
    entity.AddComponent<Gameplay::Persistent>();
    auto& health = entity.AddComponent<Gameplay::Health>();
    auto& seat = entity.AddComponent<Gameplay::VehicleSeat>();
    auto& camera = entity.CreateChild("Camera");
    camera.AddComponent<Spot::Camera>().SetEnabled(false);
    camera.AddComponent<Gameplay::ChaseCamera>();
    vehicle.viewCamera = camera.GetId();
    auto& ui = world.scene.CreateEntity("HUD");
    ui.AddComponent<Gameplay::Hud>();
    auto& panel = ui.CreateChild("Gauges");
    auto& element = panel.AddComponent<Gameplay::VehicleElement>();
    auto& count = panel.CreateChild("Speed").AddComponent<Spot::UiText>();
    element.count = count.GetEntity().GetId();
    world.Step();
    REQUIRE(seat.Enter(pawn));
    world.Step();
    CHECK(player.GetPawn() == &vehicle);
    CHECK(vehicle.GetOccupant() == &pawn);
    CHECK(pawn.HasState("Seated"));
    CHECK_FALSE(eye.GetComponent<Spot::Camera>()->IsEnabled());
    CHECK(camera.GetComponent<Spot::Camera>()->IsEnabled());
    CHECK(camera.GetTransform().GetWorldPosition().z > 3.0f);
    CHECK(element.IsShown());
    CHECK(count.text == "3");
    CHECK(Gameplay::Damage::Apply(world.scene, entity.GetId(), {.amount = 10.0f, .type = "Impact"}));
    CHECK(health.GetHealth() == Catch::Approx(90.0f));

    TempDirectory directory;
    const auto path = directory.GetPath() / "world.json";
    REQUIRE(Gameplay::WorldSave::Save(world.scene, path));
    REQUIRE(Gameplay::WorldSave::Load(world.scene, path));
    CHECK(player.GetPawn() == &pawn);
    CHECK_FALSE(pawn.HasState("Seated"));
    world.Step();
    const auto* restored = world.scene.FindComponent<TestVehicle>();
    REQUIRE(restored);
    CHECK_FALSE(restored->HasDriver());
    CHECK_FALSE(restored->GetSeat()->IsOccupied());
    CHECK_FALSE(element.IsShown());
    CHECK(eye.GetComponent<Spot::Camera>()->IsEnabled());
}

TEST_CASE("A vehicle seat rejects dead occupants and returns possession on disable", "[sandbox][gameplay][vehicles]") {
    GameplayTests::World world;
    auto& pawn = world.scene.CreateEntity("Pawn").AddComponent<Gameplay::Pawn>();
    auto& driver = world.scene.CreateEntity("Driver").AddComponent<Driver>();
    auto& vehicle = world.scene.CreateEntity("Vehicle").AddComponent<TestVehicle>();
    auto& seat = vehicle.GetEntity().AddComponent<Gameplay::VehicleSeat>();
    driver.Possess(pawn);
    world.Step();
    pawn.SetState("Dead", true);
    CHECK_FALSE(seat.Enter(pawn));
    pawn.SetState("Dead", false);
    REQUIRE(seat.Enter(pawn));
    seat.SetEnabled(false);
    CHECK(driver.GetPawn() == &pawn);
    CHECK_FALSE(pawn.HasState("Seated"));
    CHECK_FALSE(vehicle.HasDriver());
    CHECK_FALSE(seat.Enter(pawn));
}
