#include "gameplay_world.h"
#include "temp_directory.h"

#include "gameplay/core/controller.h"
#include "gameplay/core/interactor.h"
#include "gameplay/persistence/world_save.h"
#include "gameplay/vehicles/helicopter.h"
#include "gameplay/vehicles/vehicle_health.h"
#include "gameplay/vehicles/vehicle_seat.h"
#include "gameplay/vehicles/wheeled_vehicle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

namespace {

class Driver final : public Gameplay::Controller {};

Spot::Entity& MakeVehicle(Spot::Scene& scene, bool helicopter = false) {
    auto& entity = scene.CreateEntity("Vehicle");
    entity.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
    entity.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.8f, 1.0f, 3.0f});
    auto& body = entity.AddComponent<Spot::Rigidbody>();
    body.SetMass(1000.0f);
    body.SetGravityScale(0.0f);
    body.SetLinearDamping(0.0f);
    if (helicopter) {
        entity.AddComponent<Gameplay::Helicopter>();
    } else {
        entity.AddComponent<Gameplay::WheeledVehicle>();
    }
    entity.AddComponent<Gameplay::VehicleSeat>();
    entity.AddComponent<Gameplay::VehicleHealth>();
    return entity;
}

} // namespace

TEST_CASE("Vehicle health accumulates damage without regeneration", "[sandbox][gameplay][vehicles][damage]") {
    GameplayTests::World world;
    auto& entity = MakeVehicle(world.scene);
    auto& health = *entity.GetComponent<Gameplay::VehicleHealth>();
    world.Step();
    CHECK(health.GetHealth() == 300.0f);
    REQUIRE(Gameplay::Damage::Apply(world.scene, entity.GetId(), {.amount = 50.0f, .type = "Bullet"}));
    REQUIRE(Gameplay::Damage::Apply(world.scene, entity.GetId(), {.amount = 75.0f, .type = "Explosion"}));
    world.Run(8.0f);
    CHECK(health.GetHealth() == 175.0f);
    CHECK_FALSE(health.IsDestroyed());
    CHECK(entity.GetComponent<Gameplay::Vehicle>()->IsOperational());
}

TEST_CASE("A destroyed vehicle ejects its driver burns every part and blasts only once",
          "[sandbox][gameplay][vehicles][damage]") {
    const bool helicopter = GENERATE(false, true);
    GameplayTests::World world;
    auto& entity = MakeVehicle(world.scene, helicopter);
    const auto id = entity.GetId();
    auto& vehicle = *entity.GetComponent<Gameplay::Vehicle>();
    auto& health = *entity.GetComponent<Gameplay::VehicleHealth>();
    auto& seat = *entity.GetComponent<Gameplay::VehicleSeat>();
    auto& rootRenderer = entity.AddComponent<Spot::MeshRenderer>();
    auto& wheelRenderer = entity.CreateChild("Wheel").CreateChild("Rim").AddComponent<Spot::MeshRenderer>();
    health.burnedMaterial = std::make_shared<const Spot::Material>(
        Spot::Material{.baseColor = {0.03f, 0.02f, 0.01f}, .metallic = 0.0f, .roughness = 0.99f});
    auto& pawn = world.scene.CreateEntity("Driver").AddComponent<Gameplay::Pawn>();
    pawn.GetTransform().SetPosition({6.0f, 1.0f, 0.0f});
    pawn.GetEntity().AddComponent<Spot::CharacterController>();
    auto& driverHealth = pawn.GetEntity().AddComponent<Gameplay::Health>();
    auto& interactor = pawn.GetEntity().AddComponent<Gameplay::Interactor>();
    auto& driver = world.scene.CreateEntity("Controller").AddComponent<Driver>();
    driver.Possess(pawn);
    auto& nearby = world.scene.CreateEntity("Nearby");
    nearby.GetTransform().SetPosition({3.0f, 1.0f, 0.0f});
    nearby.AddComponent<Spot::BoxCollider>();
    nearby.AddComponent<Spot::Rigidbody>().SetGravityScale(0.0f);
    auto& targetHealth = nearby.AddComponent<Gameplay::Health>();
    int blasts = 0;
    auto hit =
        Gameplay::DamageSystem::Get(world.scene).OnDamageApplied.Connect([&](const Gameplay::DamageApplied& applied) {
            if (applied.target == nearby.GetId() && applied.damage.type == "Explosion") {
                ++blasts;
                CHECK(applied.damage.source == id);
                CHECK(applied.damage.instigator == pawn.GetEntity().GetId());
            }
        });
    world.Step();
    REQUIRE(seat.Enter(pawn));
    vehicle.SetInput({.drive = 1.0f, .steer = 1.0f, .lift = 1.0f});
    REQUIRE(Gameplay::Damage::Apply(world.scene, id, {.amount = 300.0f, .instigator = pawn.GetEntity().GetId()}));
    CHECK(world.scene.GetEntity(id) == &entity);
    CHECK(health.IsDead());
    CHECK(health.IsDestroyed());
    CHECK(vehicle.HasState("Destroyed"));
    CHECK_FALSE(vehicle.IsOperational());
    CHECK_FALSE(vehicle.HasDriver());
    CHECK_FALSE(seat.IsOccupied());
    CHECK_FALSE(pawn.HasState("Seated"));
    CHECK(driver.GetPawn() == &pawn);
    CHECK(driverHealth.IsDead());
    CHECK_FALSE(seat.CanInteract(interactor));
    CHECK_FALSE(seat.Enter(pawn));
    vehicle.SetInput({.drive = 1.0f, .steer = 1.0f, .lift = 1.0f});
    CHECK(vehicle.GetInput().drive == 0.0f);
    CHECK(vehicle.GetInput().steer == 0.0f);
    CHECK(vehicle.GetInput().lift == 0.0f);
    CHECK(rootRenderer.GetMaterial() == health.burnedMaterial);
    CHECK(wheelRenderer.GetMaterial() == health.burnedMaterial);
    CHECK(rootRenderer.GetTextureSpace() == Spot::TextureSpace::Object);
    CHECK(targetHealth.GetHealth() < targetHealth.maxHealth);
    CHECK(Spot::Length(nearby.GetComponent<Spot::Rigidbody>()->GetLinearVelocity()) > 0.0f);
    CHECK(blasts == 1);
    health.TakeDamage({.amount = 1000.0f});
    health.Restore();
    CHECK_FALSE(vehicle.IsOperational());
    CHECK_FALSE(seat.Enter(pawn));
    health.TakeDamage({.amount = 1000.0f});
    CHECK(blasts == 1);
    world.Step();
    CHECK(vehicle.GetThrottle() == 0.0f);
}

TEST_CASE("Vehicle checkpoints preserve damage and wrecks without repeating their blast",
          "[sandbox][gameplay][vehicles][damage][save]") {
    const bool destroyed = GENERATE(false, true);
    GameplayTests::World world;
    auto& entity = MakeVehicle(world.scene);
    entity.AddComponent<Gameplay::Persistent>();
    entity.CreateChild("Body").AddComponent<Spot::MeshRenderer>();
    auto& health = *entity.GetComponent<Gameplay::VehicleHealth>();
    world.Step();
    health.TakeDamage({.amount = destroyed ? 300.0f : 125.0f});
    TempDirectory directory;
    const auto path = directory.GetPath() / "vehicle.json";
    REQUIRE(Gameplay::WorldSave::Save(world.scene, path));
    REQUIRE(Gameplay::WorldSave::Load(world.scene, path));
    int blasts = 0;
    auto listener =
        Gameplay::DamageSystem::Get(world.scene).OnDamageApplied.Connect([&](const Gameplay::DamageApplied& applied) {
            blasts += applied.damage.type == "Explosion";
        });
    world.Step();
    auto* restored = world.scene.FindComponent<Gameplay::VehicleHealth>();
    REQUIRE(restored);
    CHECK(restored->GetHealth() == (destroyed ? 0.0f : 175.0f));
    CHECK(restored->IsDestroyed() == destroyed);
    CHECK(restored->GetEntity().GetComponent<Gameplay::Vehicle>()->IsOperational() == !destroyed);
    CHECK(blasts == 0);
    CHECK(restored->GetEntity().GetComponent<Spot::Rigidbody>()->GetLinearVelocity().y ==
          Catch::Approx(destroyed ? restored->wreckLaunchSpeed : 0.0f).margin(0.1f));
    if (destroyed) {
        const auto* renderer = restored->GetEntity().GetComponentInChildren<Spot::MeshRenderer>();
        REQUIRE(renderer);
        REQUIRE(renderer->GetMaterial());
        CHECK(renderer->GetMaterial()->roughness > 0.9f);
        CHECK(renderer->GetMaterial()->baseColor.r < 0.1f);
    }
}

TEST_CASE("Hard vehicle crashes cause damage while gentle collisions are safe",
          "[sandbox][gameplay][vehicles][damage][impact]") {
    const float speed = GENERATE(5.0f, 18.0f, 35.0f);
    GameplayTests::World world;
    auto& entity = MakeVehicle(world.scene);
    auto& wall = world.scene.CreateEntity("Wall");
    wall.GetTransform().SetPosition({0.0f, 1.0f, -5.0f});
    wall.AddComponent<Spot::BoxCollider>(Spot::Vector3{20.0f, 3.0f, 1.0f});
    auto& health = *entity.GetComponent<Gameplay::VehicleHealth>();
    world.Step();
    entity.GetComponent<Spot::Rigidbody>()->SetLinearVelocity({0.0f, 0.0f, -speed});
    world.Run(1.0f);
    if (speed <= health.safeImpactSpeed) {
        CHECK(health.GetHealth() == health.maxHealth);
    } else {
        CHECK(health.GetHealth() < health.maxHealth);
        CHECK(health.IsDestroyed() == (speed >= 35.0f));
    }
}

TEST_CASE("Vehicle explosions can trigger another damaged vehicle without exploding twice",
          "[sandbox][gameplay][vehicles][damage]") {
    GameplayTests::World world;
    auto& first = MakeVehicle(world.scene);
    auto& second = MakeVehicle(world.scene);
    second.GetTransform().SetPosition({3.0f, 1.0f, 0.0f});
    auto& firstHealth = *first.GetComponent<Gameplay::VehicleHealth>();
    auto& secondHealth = *second.GetComponent<Gameplay::VehicleHealth>();
    int firstDeaths = 0;
    int secondDeaths = 0;
    auto firstDied = firstHealth.OnDied.Connect([&](const auto&) { ++firstDeaths; });
    auto secondDied = secondHealth.OnDied.Connect([&](const auto&) { ++secondDeaths; });
    world.Step();
    secondHealth.TakeDamage({.amount = 280.0f});
    firstHealth.TakeDamage({.amount = 300.0f});
    CHECK(firstHealth.IsDestroyed());
    CHECK(secondHealth.IsDestroyed());
    CHECK(firstDeaths == 1);
    CHECK(secondDeaths == 1);
    CHECK(world.scene.GetEntity(first.GetId()));
    CHECK(world.scene.GetEntity(second.GetId()));
}

TEST_CASE("A vehicle's own blast kills nearby players while distant players remain safe",
          "[sandbox][gameplay][vehicles][damage]") {
    const float distance = GENERATE(2.0f, 4.0f, 16.0f);
    GameplayTests::World world;
    auto& entity = MakeVehicle(world.scene);
    auto& player = world.scene.CreateEntity("Player");
    player.GetTransform().SetPosition({distance, 1.0f, 0.0f});
    player.AddComponent<Spot::CharacterController>();
    auto& playerHealth = player.AddComponent<Gameplay::Health>();
    playerHealth.regenerationRate = 0.0f;
    Spot::EntityId source = Spot::EntityId::Invalid;
    auto damaged =
        playerHealth.OnDamaged.Connect([&](const Gameplay::DamageInfo& damage, float) { source = damage.source; });
    world.Step();
    auto& health = *entity.GetComponent<Gameplay::VehicleHealth>();
    health.TakeDamage({.amount = health.maxHealth});
    if (distance < 5.0f) {
        CHECK(playerHealth.IsDead());
        CHECK(source == entity.GetId());
    } else {
        CHECK(playerHealth.GetHealth() == playerHealth.maxHealth);
        CHECK(source == Spot::EntityId::Invalid);
    }
}

TEST_CASE("A vehicle's own explosion launches and turns its wreck without discarding its momentum",
          "[sandbox][gameplay][vehicles][damage]") {
    const bool helicopter = GENERATE(false, true);
    const float mass = GENERATE(1000.0f, 2000.0f);
    GameplayTests::World world;
    auto& entity = MakeVehicle(world.scene, helicopter);
    auto& body = *entity.GetComponent<Spot::Rigidbody>();
    body.SetMass(mass);
    world.Step();
    body.SetLinearVelocity({2.0f, 0.0f, 0.0f});
    auto& health = *entity.GetComponent<Gameplay::VehicleHealth>();
    const auto start = entity.GetTransform().GetWorldPosition();
    health.TakeDamage({.amount = health.maxHealth});
    CHECK_FALSE(body.IsKinematic());
    CHECK_FALSE(body.IsSleeping());
    CHECK(body.GetLinearVelocity().x == Catch::Approx(2.0f));
    CHECK(body.GetLinearVelocity().y == Catch::Approx(health.wreckLaunchSpeed));
    CHECK(Spot::Length(body.GetAngularVelocity()) > 0.5f);
    const auto velocity = body.GetLinearVelocity();
    health.Restore();
    health.TakeDamage({.amount = health.maxHealth});
    CHECK(body.GetLinearVelocity().y == Catch::Approx(velocity.y));
    world.Run(0.2f);
    CHECK(entity.GetTransform().GetWorldPosition().y > start.y + 0.5f);
    CHECK(entity.GetTransform().GetWorldPosition().x > start.x + 0.2f);
    CHECK(Spot::Dot(entity.GetTransform().GetWorldRotation() * Spot::Vector3{0.0f, 1.0f, 0.0f},
                    Spot::Vector3{0.0f, 1.0f, 0.0f}) < 0.99f);
}
