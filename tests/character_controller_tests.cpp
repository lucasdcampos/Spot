#include "spot/scene/physics/physics_world.h"
#include <spot.h>
#include <spot/scene/physics/collision_listener.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using Catch::Approx;
using Spot::BoxCollider;
using Spot::CharacterController;
using Spot::Entity;
using Spot::Scene;
using Spot::Vector3;

namespace {

constexpr float Step = Scene::DefaultFixedTimeStep;

void Simulate(Scene& scene, float seconds) {
    const auto updates = std::lround(seconds / Step);
    for (long i = 0; i < updates; ++i) {
        scene.Update(Step);
    }
}

// Walks with a velocity, falling with gravity while in the air, and records its physics events.
class Walker final : public Spot::Component, public Spot::CollisionListener, public Spot::TriggerListener {
public:
    Vector3 walk = {};
    std::vector<std::string> events;

    bool Has(const std::string& event) const { return std::ranges::find(events, event) != events.end(); }

protected:
    void OnFixedUpdate(float deltaTime) override {
        auto& controller = *GetEntity().GetComponent<CharacterController>();
        m_fall = controller.IsGrounded() ? 0.0f : m_fall - 9.81f * deltaTime;
        controller.Move(walk + Vector3{0.0f, m_fall, 0.0f}, deltaTime);
    }
    void OnCollisionEnter(const Spot::Collision& collision) override {
        events.push_back("enter " + Name(collision.other));
    }
    void OnCollisionExit(const Spot::Collision& collision) override {
        events.push_back("exit " + Name(collision.other));
    }
    void OnTriggerEnter(const Spot::TriggerContact& contact) override {
        events.push_back("trigger " + Name(contact.other));
    }

private:
    std::string Name(Spot::EntityId id) const {
        const Entity* entity = GetScene().GetEntity(id);
        return entity ? entity->GetName() : "(gone)";
    }

    float m_fall = 0.0f;
};

Entity& CreateBox(Scene& scene, const char* name, Vector3 position, Vector3 size) {
    auto& entity = scene.CreateEntity(name);
    entity.GetTransform().SetPosition(position);
    entity.AddComponent<BoxCollider>(size);
    return entity;
}

// A ground whose top is at y = 0, and a character standing on it at the origin, its capsule 1.8 tall.
struct CharacterScene {
    Scene scene;
    Entity& ground = CreateBox(scene, "Ground", {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f});
    Entity& character = scene.CreateEntity("Character");
    CharacterController& controller = character.AddComponent<CharacterController>();
    Walker& walker = character.AddComponent<Walker>();

    explicit CharacterScene(Vector3 start = {0.0f, 0.95f, 0.0f}) { character.GetTransform().SetPosition(start); }

    Vector3 Position() const { return character.GetTransform().GetPosition(); }
};

} // namespace

TEST_CASE("Characters fall onto the ground and stand on it", "[physics][character]") {
    CharacterScene world({0.0f, 3.0f, 0.0f});
    Simulate(world.scene, 0.2f);
    CHECK_FALSE(world.controller.IsGrounded());
    CHECK(world.controller.GetGround() == Spot::EntityId::Invalid);

    Simulate(world.scene, 2.0f);
    CHECK(world.controller.IsGrounded());
    CHECK(world.controller.GetGround() == world.ground.GetId());
    CHECK(world.controller.GetGroundNormal().y == Approx(1.0f).margin(0.01f));
    // The capsule's bottom rests on the ground.
    CHECK(world.Position().y == Approx(0.9f).margin(0.05f));
    CHECK(world.walker.Has("enter Ground"));
}

TEST_CASE("Characters stop at walls and slide along them", "[physics][character]") {
    CharacterScene world;
    CreateBox(world.scene, "Wall", {3.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 10.0f});
    world.walker.walk = {4.0f, 0.0f, 2.0f};
    Simulate(world.scene, 2.0f);
    // Held by the wall's face at x = 2.5, a radius away, while still going along it.
    CHECK(world.Position().x == Approx(2.1f).margin(0.05f));
    CHECK(world.Position().z > 2.0f);
    CHECK(world.walker.Has("enter Wall"));
    CHECK(world.controller.IsGrounded());
}

TEST_CASE("Characters climb steps up to their step height", "[physics][character]") {
    CharacterScene world;
    world.controller.SetStepHeight(0.35f);
    CreateBox(world.scene, "Low", {3.0f, 0.15f, 0.0f}, {2.0f, 0.3f, 4.0f});
    CreateBox(world.scene, "High", {3.0f, 0.3f, 6.0f}, {2.0f, 0.6f, 4.0f});

    world.walker.walk = {3.0f, 0.0f, 0.0f};
    Simulate(world.scene, 1.2f);
    CHECK(world.Position().x > 2.5f);
    CHECK(world.Position().y == Approx(1.2f).margin(0.06f));

    CharacterScene blocked({0.0f, 0.95f, 6.0f});
    blocked.controller.SetStepHeight(0.35f);
    CreateBox(blocked.scene, "High", {3.0f, 0.3f, 6.0f}, {2.0f, 0.6f, 4.0f});
    blocked.walker.walk = {3.0f, 0.0f, 0.0f};
    Simulate(blocked.scene, 1.2f);
    CHECK(blocked.Position().x < 1.7f);
}

TEST_CASE("Characters stand on gentle slopes and slide off steep ones", "[physics][character]") {
    const auto standOn = [](float degrees) {
        CharacterScene world({0.0f, 6.0f, 0.0f});
        auto& slope = CreateBox(world.scene, "Slope", {0.0f, 2.0f, 0.0f}, {6.0f, 0.5f, 6.0f});
        slope.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, Spot::ToRadians(degrees)));
        Simulate(world.scene, 1.5f);
        return world.controller.IsGrounded() && world.controller.GetGround() == slope.GetId();
    };
    CHECK(standOn(20.0f));
    CHECK_FALSE(standOn(60.0f));
}

TEST_CASE("Characters push dynamic bodies out of their way", "[physics][character]") {
    CharacterScene world;
    auto& crate = CreateBox(world.scene, "Crate", {2.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f});
    crate.AddComponent<Spot::Rigidbody>().SetMass(5.0f);
    world.walker.walk = {3.0f, 0.0f, 0.0f};
    Simulate(world.scene, 2.0f);
    CHECK(crate.GetTransform().GetPosition().x > 3.0f);
}

TEST_CASE("Character support distinguishes a steep foothold from a wall touching the side", "[physics][character]") {
    SECTION("A steep foothold can hold still without allowing uphill walking") {
        Scene scene;
        auto& slope = CreateBox(scene, "Slope", {0, 5, 0}, {30, 0.5f, 20});
        slope.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({0, 0, 1}, Spot::ToRadians(70.0f)));
        auto& entity = scene.CreateEntity("Character");
        auto& controller = entity.AddComponent<CharacterController>();
        controller.SetStepHeight(1.0f);
        entity.GetTransform().SetPosition({0, 9, 0});
        float fall = 0.0f;
        for (int i = 0; i < 90; ++i) {
            scene.Update(Step);
            fall = controller.IsSupported() ? 0.0f : fall - 24.0f * Step;
            controller.Move({0, fall, 0}, Step);
        }
        const auto position = entity.GetTransform().GetPosition();
        const auto normal = controller.GetGroundNormal();
        INFO("Position " << position.x << ", " << position.y << ", " << position.z << "; normal " << normal.x << ", "
                         << normal.y << ", " << normal.z);
        REQUIRE(controller.IsSupported());
        CHECK_FALSE(controller.IsGrounded());
        const auto resting = entity.GetTransform().GetPosition();
        for (int i = 0; i < 60; ++i) {
            scene.Update(Step);
            controller.Move({}, Step);
        }
        CHECK(Spot::Length(entity.GetTransform().GetPosition() - resting) < 0.005f);
        for (int i = 0; i < 60; ++i) {
            scene.Update(Step);
            controller.Move({3, 0, 0}, Step);
        }
        CHECK(entity.GetTransform().GetPosition().y < resting.y + 0.1f);
    }
    SECTION("A vertical wall cannot suspend a falling character") {
        Scene scene;
        CreateBox(scene, "Wall", {2, 5, 0}, {1, 20, 10});
        auto& entity = scene.CreateEntity("Character");
        auto& controller = entity.AddComponent<CharacterController>();
        entity.GetTransform().SetPosition({1.05f, 5, 0});
        for (int i = 0; i < 60; ++i) {
            scene.Update(Step);
            controller.Move({4, -3, 0}, Step);
            CHECK_FALSE(controller.IsSupported());
            CHECK_FALSE(controller.IsGrounded());
        }
        CHECK(entity.GetTransform().GetPosition().y < 2.1f);
    }
}

TEST_CASE("Characters enter triggers, keep to their layer and are seen by queries", "[physics][character]") {
    CharacterScene world;
    auto& zone = CreateBox(world.scene, "Zone", {2.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 4.0f});
    zone.GetComponent<BoxCollider>()->SetTrigger(true);
    auto& wall = CreateBox(world.scene, "Wall", {5.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 10.0f});
    wall.SetLayer(5);
    world.character.SetLayer(3);
    world.scene.GetOrAddSystem<Spot::PhysicsWorld>().SetLayerCollision(3, 5, false);

    world.walker.walk = {4.0f, 0.0f, 0.0f};
    Simulate(world.scene, 2.0f);
    CHECK(world.walker.Has("trigger Zone"));
    // Its layer does not collide with the wall's, so it walked through.
    CHECK(world.Position().x > 6.0f);

    const auto hit = world.scene.GetOrAddSystem<Spot::PhysicsWorld>().Raycast(
        world.Position() + Vector3{0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == world.character.GetId());
    // Ignoring the entity goes through its character too.
    const auto through = world.scene.GetOrAddSystem<Spot::PhysicsWorld>().Raycast(
        world.Position() + Vector3{0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, {.ignore = world.character.GetId()});
    REQUIRE(through.has_value());
    CHECK(through->entity == world.ground.GetId());
}

TEST_CASE("Character simulations can be destroyed and recreated with physics jobs in flight", "[physics][character]") {
    for (int i = 0; i < 40; ++i) {
        CharacterScene world;
        world.walker.walk = {3.0f, 0.0f, 0.0f};
        Simulate(world.scene, 4.0f * Step);
        CHECK(world.Position().x > 0.15f);
    }
}

TEST_CASE("Characters are saved with their scene, and follow their entity when it is placed", "[physics][character]") {
    CharacterScene world;
    world.controller.SetRadius(0.3f);
    Simulate(world.scene, 0.5f);
    world.character.GetTransform().SetPosition({10.0f, 0.95f, 0.0f});
    Simulate(world.scene, 0.5f);
    CHECK(world.Position().x == Approx(10.0f).margin(0.01f));
    CHECK(world.controller.IsGrounded());

    Scene loaded;
    REQUIRE(loaded.LoadFromString(world.scene.SaveToString()));
    const auto* controller = loaded.FindComponent<CharacterController>();
    REQUIRE(controller);
    CHECK(controller->GetRadius() == 0.3f);
}
