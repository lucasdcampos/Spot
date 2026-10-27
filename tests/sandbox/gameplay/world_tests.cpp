#include "gameplay_world.h"

#include <spot.h>
#include <spot/scene/physics/physics_world.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("Gameplay tests run in a world of a bare ground", "[sandbox][gameplay]") {
    GameplayTests::World world;
    CHECK(world.scene.GetEntityCount() == 1);

    auto& crate = world.scene.CreateEntity("Crate");
    crate.GetTransform().SetPosition({3.0f, 2.0f, 0.0f});
    crate.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.0f, 1.0f, 1.0f});
    crate.AddComponent<Spot::Rigidbody>();
    world.Run(2.0f);
    // Tests place what stands on the ground by its top, at y = 0.
    const auto ground = world.scene.GetOrAddSystem<Spot::PhysicsWorld>().Raycast({0, 5, 0}, {0, -1, 0}, 10);
    REQUIRE(ground);
    CHECK(ground->entity == world.ground.GetId());
    CHECK(ground->point.y == Approx(0.0f).margin(0.001f));
    // Bodies settle into it by up to the physics' penetration slop.
    CHECK(crate.GetTransform().GetPosition().y == Approx(0.5f).margin(0.03f));
}
