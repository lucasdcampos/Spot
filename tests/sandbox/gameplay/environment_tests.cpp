#include "gameplay_world.h"

#include "gameplay/core/trace_volume.h"
#include "gameplay/effects/effect_pool.h"
#include "gameplay/environment/day_cycle.h"
#include "gameplay/environment/night_light.h"
#include "gameplay/environment/water.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("Water is a volume traces meet at its surface and blasts at or under it", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& pond = world.scene.CreateEntity("Pond");
    pond.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
    pond.GetTransform().SetScale({10.0f, 1.0f, 10.0f});
    auto& water = pond.AddComponent<Gameplay::Water>();
    world.Step();

    const auto hit = Gameplay::Traces::Raycast(world.scene, {0.0f, 6.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
    REQUIRE(hit);
    CHECK(hit->volume == &water);
    CHECK(hit->distance == Approx(5.0f));
    // Not from under it, beside it, or short of it.
    CHECK_FALSE(Gameplay::Traces::Raycast(world.scene, {0.0f, 0.5f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f));
    CHECK_FALSE(Gameplay::Traces::Raycast(world.scene, {20.0f, 6.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f));
    CHECK_FALSE(Gameplay::Traces::Raycast(world.scene, {0.0f, 6.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 2.0f));
    CHECK(water.GetShotDepth() == water.shotDepth);

    CHECK(Gameplay::Traces::Blast(world.scene, {0.0f, 20.0f, 0.0f}, 3.0f) == Gameplay::BlastMedium::None);
    CHECK(Gameplay::Traces::Blast(world.scene, {0.0f, 1.2f, 0.0f}, 3.0f) == Gameplay::BlastMedium::Surface);
    CHECK(Gameplay::Traces::Blast(world.scene, {0.0f, 0.0f, 0.0f}, 3.0f) == Gameplay::BlastMedium::Inside);
}

TEST_CASE("The day cycle lights effects and street lamps as the day goes", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& sun = world.scene.CreateEntity("Sun");
    sun.AddComponent<Spot::DirectionalLight>();
    auto& cycle = sun.AddComponent<Gameplay::DayCycle>();
    cycle.dayLength = 0.0f;
    auto& lamp = world.scene.CreateEntity("Lamp");
    lamp.AddComponent<Spot::PointLight>();
    lamp.AddComponent<Gameplay::NightLight>();
    world.Step();

    cycle.SetTimeOfDay(0.5f);
    world.Step();
    CHECK(cycle.GetDaylight() == Approx(1.0f));
    CHECK(Gameplay::EffectPool::Get(world.scene).GetLight() == Approx(1.0f));
    CHECK_FALSE(lamp.GetComponent<Spot::PointLight>()->IsEnabled());

    cycle.SetTimeOfDay(0.0f);
    world.Step();
    CHECK(cycle.GetDaylight() == Approx(0.0f));
    CHECK(Gameplay::EffectPool::Get(world.scene).GetLight() == Approx(0.12f));
    CHECK(lamp.GetComponent<Spot::PointLight>()->IsEnabled());
}
