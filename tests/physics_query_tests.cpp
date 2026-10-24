#include <spot.h>
#include <spot/scene/physics/physics_world.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;
using Spot::BoxCollider;
using Spot::Entity;
using Spot::Rigidbody;
using Spot::Scene;
using Spot::SphereCollider;
using Spot::Vector3;

namespace {

constexpr Vector3 Down{0.0f, -1.0f, 0.0f};

Entity& CreateBox(Scene& scene, const char* name, Vector3 position, Vector3 size) {
    auto& entity = scene.CreateEntity(name);
    entity.GetTransform().SetPosition(position);
    entity.AddComponent<BoxCollider>(size);
    return entity;
}

// A ground whose top is at y = 0, a crate on it at x = 3, and a trigger zone above the origin.
struct QueryScene {
    Scene scene;
    Entity& ground = CreateBox(scene, "Ground", {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
    Entity& crate = CreateBox(scene, "Crate", {3.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f});
    Entity& zone = CreateBox(scene, "Zone", {0.0f, 2.0f, 0.0f}, {2.0f, 1.0f, 2.0f});

    QueryScene() {
        zone.GetComponent<BoxCollider>()->SetTrigger(true);
        // Colliders start, and so join the physics, in the first update.
        scene.Update(0.0f);
    }

    Spot::PhysicsWorld& Physics() { return scene.GetOrAddSystem<Spot::PhysicsWorld>(); }
};

} // namespace

TEST_CASE("Rays find the first body in their way, with its collider, point, normal and distance",
          "[physics][queries]") {
    QueryScene world;
    const auto hit = world.Physics().Raycast({0.0f, 5.0f, 0.0f}, Down, 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == world.ground.GetId());
    CHECK(hit->collider == world.ground.GetComponent<BoxCollider>());
    CHECK(hit->point.y == Approx(0.0f).margin(1e-3f));
    CHECK(hit->normal.y == Approx(1.0f).margin(1e-3f));
    CHECK(hit->distance == Approx(5.0f).margin(1e-3f));

    // Not beyond its distance, and directions need not be unit vectors.
    CHECK_FALSE(world.Physics().Raycast({0.0f, 5.0f, 0.0f}, Down, 4.0f).has_value());
    const auto crate = world.Physics().Raycast({0.0f, 0.5f, 0.0f}, {5.0f, 0.0f, 0.0f}, 10.0f);
    REQUIRE(crate.has_value());
    CHECK(crate->entity == world.crate.GetId());
    CHECK(crate->distance == Approx(2.5f).margin(1e-3f));
    CHECK(crate->normal.x == Approx(-1.0f).margin(1e-3f));
}

TEST_CASE("Query filters pick layers, triggers and an entity to ignore", "[physics][queries]") {
    QueryScene world;
    const Vector3 above{0.0f, 5.0f, 0.0f};
    // Triggers are left out unless asked for.
    CHECK(world.Physics().Raycast(above, Down, 10.0f)->entity == world.ground.GetId());
    const auto trigger = world.Physics().Raycast(above, Down, 10.0f, {.includeTriggers = true});
    REQUIRE(trigger.has_value());
    CHECK(trigger->entity == world.zone.GetId());
    CHECK(trigger->collider->IsTrigger());

    CHECK_FALSE(world.Physics().Raycast(above, Down, 10.0f, {.ignore = world.ground.GetId()}).has_value());

    world.ground.SetLayer(4);
    CHECK_FALSE(world.Physics().Raycast(above, Down, 10.0f, {.layerMask = ~(1u << 4)}).has_value());
    CHECK(world.Physics().Raycast(above, Down, 10.0f, {.layerMask = 1u << 4}).has_value());
}

TEST_CASE("RaycastAll finds every collider in the way, nearest first", "[physics][queries]") {
    QueryScene world;
    const auto hits = world.Physics().RaycastAll({-5.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, 20.0f);
    // The ray runs along the top of the ground at y = 0.5, through nothing but the crate.
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].entity == world.crate.GetId());

    const auto down = world.Physics().RaycastAll({3.0f, 5.0f, 0.0f}, Down, 10.0f);
    REQUIRE(down.size() == 2);
    CHECK(down[0].entity == world.crate.GetId());
    CHECK(down[1].entity == world.ground.GetId());
    CHECK(down[0].distance < down[1].distance);
}

TEST_CASE("Sphere casts and overlaps find the bodies a shape meets", "[physics][queries]") {
    QueryScene world;
    const auto hit = world.Physics().SphereCast({0.0f, 5.0f, 0.0f}, 0.5f, Down, 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == world.ground.GetId());
    CHECK(hit->distance == Approx(4.5f).margin(0.01f));
    CHECK(hit->point.y == Approx(0.0f).margin(0.01f));
    CHECK(hit->normal.y == Approx(1.0f).margin(0.01f));

    const auto overlaps = world.Physics().OverlapSphere({3.0f, 1.2f, 0.0f}, 0.5f);
    REQUIRE(overlaps.size() == 1);
    CHECK(overlaps[0].entity == world.crate.GetId());
    CHECK(overlaps[0].collider == world.crate.GetComponent<BoxCollider>());

    const auto both = world.Physics().OverlapBox({1.5f, 0.0f, 0.0f}, {4.0f, 0.5f, 1.0f});
    CHECK(both.size() == 2);
    const auto withTrigger =
        world.Physics().OverlapBox({0.0f, 1.0f, 0.0f}, {1.0f, 3.0f, 1.0f}, {}, {.includeTriggers = true});
    CHECK(std::ranges::any_of(withTrigger,
                              [&](const Spot::OverlapHit& found) { return found.entity == world.zone.GetId(); }));
}

TEST_CASE("Queries see colliders changed since the last step", "[physics][queries]") {
    QueryScene world;
    const Vector3 beyond{15.0f, 5.0f, 0.0f};
    CHECK_FALSE(world.Physics().Raycast(beyond, Down, 10.0f).has_value());
    world.ground.GetComponent<BoxCollider>()->SetSize({40.0f, 1.0f, 40.0f});
    CHECK(world.Physics().Raycast(beyond, Down, 10.0f).has_value());

    // Several colliders of an entity are told apart.
    auto& second = world.crate.AddComponent<SphereCollider>();
    second.SetCenter({0.0f, 1.0f, 0.0f});
    world.scene.Update(0.0f);
    const auto top = world.Physics().Raycast({3.0f, 5.0f, 0.0f}, Down, 10.0f);
    REQUIRE(top.has_value());
    CHECK(top->collider == &second);
}
