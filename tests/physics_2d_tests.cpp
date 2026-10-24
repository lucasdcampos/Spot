#include "spot/scene/physics2d/physics_world_2d.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <spot.h>
#include <spot/scene/physics2d/collision_listener_2d.h>
#include <stdexcept>

using namespace Spot;
using Catch::Approx;
namespace {
constexpr float Step = Scene::DefaultFixedTimeStep;
class Contacts2D final : public Component, public CollisionListener2D, public TriggerListener2D {
public:
    int enters = 0, stays = 0, exits = 0, triggers = 0, triggerExits = 0;
    bool destroyOther = false, fail = false;
    Collision2D last;

protected:
    void OnCollisionEnter2D(const Collision2D& contact) override {
        ++enters;
        last = contact;
        if (destroyOther)
            if (auto* other = GetScene().GetEntity(contact.other))
                GetScene().DestroyEntity(*other);
        if (fail)
            throw std::runtime_error("2D contact failure");
    }
    void OnCollisionStay2D(const Collision2D&) override { ++stays; }
    void OnCollisionExit2D(const Collision2D&) override { ++exits; }
    void OnTriggerEnter2D(const TriggerContact2D&) override { ++triggers; }
    void OnTriggerExit2D(const TriggerContact2D&) override { ++triggerExits; }
};
void Steps(Scene& scene, int count) {
    for (int i = 0; i < count; ++i)
        scene.Update(Step);
}
} // namespace

TEST_CASE("2D physics code-only bodies fall, collide and retain stable handles", "[physics2d]") {
    Physics2D physics;
    BodyDesc2D ground{.position = {0.0f, -0.5f}, .shapes = {Shape2D{.size = {20.0f, 1.0f}}}};
    auto floor = physics.CreateBody(ground);
    for (auto kind : {ShapeKind2D::Box, ShapeKind2D::Circle, ShapeKind2D::Capsule, ShapeKind2D::Polygon}) {
        Shape2D shape{.kind = kind};
        if (kind == ShapeKind2D::Polygon)
            shape.vertices = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.5f}};
        auto body = physics.CreateBody({.motion = BodyMotion2D::Dynamic, .position = {0.0f, 3.0f}, .shapes = {shape}});
        REQUIRE(body != BodyId2D::Invalid);
        for (int i = 0; i < 180; ++i)
            physics.Step(Step);
        CHECK(physics.GetPose(body).position.y > 0.45f);
        CHECK(physics.GetPose(body).position.y < 1.05f);
        physics.DestroyBody(body);
        physics.AddImpulse(body, {100.0f, 100.0f});
        CHECK(physics.GetLinearVelocity(body) == Vector2{});
    }
    physics.DestroyBody(floor);
    CHECK(physics.CreateBody(ground) != floor);
}
TEST_CASE("2D physics forces mass rotation sleep and validation", "[physics2d]") {
    Physics2D physics;
    physics.SetGravity({});
    auto body = physics.CreateBody({.motion = BodyMotion2D::Dynamic, .shapes = {Shape2D{}}, .mass = 2.0f});
    physics.AddImpulse(body, {4.0f, 0.0f});
    CHECK(physics.GetLinearVelocity(body).x == Approx(2.0f));
    physics.AddForce(body, {120.0f, 0.0f});
    physics.Step(Step);
    CHECK(physics.GetLinearVelocity(body).x == Approx(3.0f).margin(0.01f));
    physics.AddImpulseAtPosition(body, {0.0f, 1.0f}, {1.0f, 0.0f});
    CHECK(physics.GetAngularVelocity(body) > 0.0f);
    physics.Sleep(body);
    CHECK_FALSE(physics.IsAwake(body));
    physics.WakeUp(body);
    CHECK(physics.IsAwake(body));
    CHECK(physics.CreateBody({.mass = -1.0f}) == BodyId2D::Invalid);
    CHECK(physics.CreateBody({.position = {std::numeric_limits<float>::infinity(), 0.0f}}) == BodyId2D::Invalid);
    Shape2D polygon{.kind = ShapeKind2D::Polygon, .vertices = {{0, 0}, {1, 0}, {2, 0}}};
    CHECK(physics.CreateBody({.shapes = {polygon}}) == BodyId2D::Invalid);
}
TEST_CASE("2D physics layers continuous collision and rotation locks constrain motion", "[physics2d]") {
    Physics2D physics;
    physics.SetGravity({});
    auto wall = physics.CreateBody({.position = {2, 0}, .shapes = {Shape2D{.size = {0.1f, 10}}}, .layer = 1});
    auto fast = physics.CreateBody({.motion = BodyMotion2D::Dynamic,
                                    .shapes = {Shape2D{.kind = ShapeKind2D::Circle, .radius = 0.1f}},
                                    .linearVelocity = {200, 0},
                                    .lockRotation = true,
                                    .continuous = true});
    physics.AddTorque(fast, 100);
    physics.Step(Step);
    CHECK(physics.GetPose(fast).position.x < 2);
    CHECK(physics.GetAngularVelocity(fast) == 0);
    std::array<std::uint32_t, 32> masks;
    masks.fill(0xffffffffu);
    masks[0] &= ~(1u << 1u);
    masks[1] &= ~1u;
    physics.SetLayerMasks(masks);
    physics.SetLinearVelocity(fast, {200, 0});
    physics.Step(Step);
    CHECK(physics.GetPose(fast).position.x > 2);
    physics.DestroyBody(wall);
    auto kinematic = physics.CreateBody({.motion = BodyMotion2D::Kinematic, .shapes = {Shape2D{}}});
    physics.MoveKinematic(kinematic, {{1, 0}, 0.2f}, Step);
    physics.Step(Step);
    CHECK(physics.GetPose(kinematic).position.x == Approx(1));
    CHECK(physics.GetPose(kinematic).angle == Approx(0.2f).margin(0.001f));
}

TEST_CASE("2D scene queries filter layers triggers and edited compound colliders", "[physics2d]") {
    Scene scene;
    auto& world = scene.GetOrAddSystem<Spot::PhysicsWorld2D>();
    world.SetGravity({});
    auto& box = scene.CreateEntity("Box");
    box.GetTransform().SetPosition({3, 0, 7});
    box.SetLayer(2);
    auto& collider = box.AddComponent<BoxCollider2D>();
    auto& trigger = box.AddComponent<CircleCollider2D>();
    trigger.SetTrigger(true);
    trigger.SetRadius(2.0f);
    auto hit = world.Raycast({0, 0}, {2, 0}, 10);
    REQUIRE(hit);
    CHECK(hit->entity == box.GetId());
    CHECK(hit->collider == &collider);
    CHECK(hit->distance == Approx(2.5f));
    CHECK_FALSE(world.Raycast({0, 0}, {1, 0}, 10, {.layerMask = 1}));
    CHECK_FALSE(world.Raycast({0, 0}, {1, 0}, 10, {.ignore = box.GetId()}));
    CHECK(world.RaycastAll({0, 0}, {1, 0}, 10, {.includeTriggers = true}).size() == 2);
    collider.SetSize({2, 2});
    CHECK(world.Raycast({0, 0}, {1, 0}, 10)->distance == Approx(2.0f));
    CHECK(world.CircleCast({0, 0}, 0.5f, {1, 0}, 10)->distance == Approx(1.5f).margin(0.02f));
    CHECK(world.CircleCast({3, 0}, 0.5f, {1, 0}, 10)->distance == 0.0f);
    CHECK(world.OverlapCircle({3, 0}, 0.1f).size() == 1);
    CHECK(world.OverlapBox({3, 0}, {0.1f, 0.1f}, 0.7f).size() == 1);
    collider.SetEnabled(false);
    CHECK_FALSE(world.Raycast({0, 0}, {1, 0}, 10));
    world.SetLayerCollision(2, 4, false);
    CHECK_FALSE(world.GetLayerCollision(4, 2));
}
TEST_CASE("2D scene contacts survive compound shapes edits destruction and hook failure", "[physics2d]") {
    Scene scene;
    scene.GetOrAddSystem<Spot::PhysicsWorld2D>().SetGravity({});
    auto& a = scene.CreateEntity("A");
    a.AddComponent<BoxCollider2D>();
    a.AddComponent<CircleCollider2D>();
    auto& body = a.AddComponent<Rigidbody2D>();
    body.SetGravityScale(0);
    body.SetRotationLocked(true);
    auto& events = a.AddComponent<Contacts2D>();
    auto& b = scene.CreateEntity("B");
    b.GetTransform().SetPosition({0.8f, 0, 0});
    b.AddComponent<BoxCollider2D>();
    Steps(scene, 2);
    CHECK(events.enters == 1);
    CHECK(events.stays == 1);
    CHECK(events.last.normal.x < 0.0f);
    scene.DestroyEntity(b);
    Steps(scene, 1);
    CHECK(events.exits == 1);
    auto& sensor = scene.CreateEntity("Sensor");
    sensor.AddComponent<BoxCollider2D>(Vector2{10, 10}).SetTrigger(true);
    Steps(scene, 1);
    CHECK(events.triggers == 1);
    sensor.SetActive(false);
    Steps(scene, 1);
    CHECK(events.triggerExits == 1);
    auto& c = scene.CreateEntity("C");
    c.GetTransform().SetPosition(a.GetTransform().GetPosition());
    c.AddComponent<BoxCollider2D>();
    events.fail = true;
    Steps(scene, 1);
    CHECK(events.IsFaulted());
    CHECK_FALSE(scene.GetOrAddSystem<Spot::PhysicsWorld2D>().IsFaulted());
}
TEST_CASE("2D scene dynamics preserve hierarchy depth and rebuild velocity", "[physics2d]") {
    Scene scene;
    auto& parent = scene.CreateEntity("Parent");
    parent.GetTransform().SetPosition({10, 0, 2});
    auto& child = scene.CreateEntity("Child");
    child.SetParent(&parent);
    child.GetTransform().SetPosition({0, 3, 5});
    child.AddComponent<CapsuleCollider2D>();
    auto& body = child.AddComponent<Rigidbody2D>();
    body.SetMass(2);
    body.AddImpulse({4, 0});
    Steps(scene, 1);
    CHECK(body.GetLinearVelocity().x == Approx(2));
    CHECK(child.GetTransform().GetWorldPosition().z == Approx(7));
    body.SetGravityScale(0);
    Steps(scene, 1);
    CHECK(body.GetLinearVelocity().x == Approx(2));
    body.SetEnabled(false);
    Steps(scene, 1);
    auto position = child.GetTransform().GetWorldPosition();
    Steps(scene, 10);
    CHECK(child.GetTransform().GetWorldPosition() == position);
    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    auto* restored = loaded.FindEntity("Child");
    REQUIRE(restored);
    CHECK(restored->GetComponent<Rigidbody2D>()->GetMass() == 2);
    CHECK(restored->HasComponent<CapsuleCollider2D>());
}
TEST_CASE("2D scene callbacks can destroy the other body and query safely", "[physics2d]") {
    Scene scene;
    scene.GetOrAddSystem<Spot::PhysicsWorld2D>().SetGravity({});
    auto& a = scene.CreateEntity();
    a.AddComponent<BoxCollider2D>();
    a.AddComponent<Rigidbody2D>();
    auto& events = a.AddComponent<Contacts2D>();
    events.destroyOther = true;
    auto& b = scene.CreateEntity();
    b.AddComponent<BoxCollider2D>();
    auto id = b.GetId();
    Steps(scene, 1);
    CHECK(events.enters == 1);
    CHECK(scene.GetEntity(id) == nullptr);
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld2D>().OverlapBox({0, 0}, {10, 10}).size() == 1);
    Steps(scene, 1);
    CHECK(events.exits == 1);
}
