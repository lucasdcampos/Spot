#include "registered_assets.h"
#include <spot.h>
#include <spot/scene/physics/physics_world.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using Catch::Approx;
using Spot::BoxCollider;
using Spot::CapsuleCollider;
using Spot::Entity;
using Spot::PhysicsWorld;
using Spot::Rigidbody;
using Spot::Scene;
using Spot::SphereCollider;
using Spot::Vector3;

namespace {

constexpr float Step = Scene::DefaultFixedTimeStep;
// Resting bodies sink into what holds them by up to this much.
constexpr float RestMargin = 0.03f;

// Runs updates of one fixed step each, so every update steps the physics once.
void Simulate(Scene& scene, float seconds) {
    const auto updates = std::lround(seconds / Step);
    for (long i = 0; i < updates; ++i) {
        scene.Update(Step);
    }
}

// A static floor whose top is at y = 0.
Entity& CreateGround(Scene& scene) {
    auto& ground = scene.CreateEntity("Ground");
    ground.GetTransform().SetPosition({0.0f, -0.5f, 0.0f});
    ground.AddComponent<BoxCollider>(Vector3{20.0f, 1.0f, 20.0f});
    return ground;
}

Entity& CreateBall(Scene& scene, Vector3 position) {
    auto& ball = scene.CreateEntity("Ball");
    ball.GetTransform().SetPosition(position);
    ball.AddComponent<SphereCollider>();
    ball.AddComponent<Rigidbody>();
    return ball;
}

float GetHeight(const Entity& entity) {
    return entity.GetTransform().GetPosition().y;
}

class FixedUpdateCounter final : public Spot::Component {
public:
    int count = 0;
    float lastDeltaTime = 0.0f;

protected:
    void OnFixedUpdate(float fixedDeltaTime) override {
        ++count;
        lastDeltaTime = fixedDeltaTime;
    }
};

class Thruster final : public Spot::Component {
public:
    explicit Thruster(Vector3 force) : m_force(force) {}

protected:
    void OnFixedUpdate(float /*fixedDeltaTime*/) override { GetEntity().GetComponent<Rigidbody>()->AddForce(m_force); }

private:
    Vector3 m_force;
};

} // namespace

TEST_CASE("A rigidbody falls with gravity", "[physics]") {
    Scene scene;
    auto& body = scene.CreateEntity("Body");
    body.AddComponent<Rigidbody>();

    Simulate(scene, 1.0f);

    CHECK(GetHeight(body) == Approx(-0.5f * 9.81f).margin(0.15f));
    CHECK(body.GetComponent<Rigidbody>()->GetLinearVelocity().y == Approx(-9.81f).margin(0.01f));
}

TEST_CASE("Static, kinematic and weightless bodies do not fall", "[physics]") {
    Scene scene;
    auto& wall = scene.CreateEntity("Wall");
    wall.GetTransform().SetPosition({0.0f, 5.0f, 0.0f});
    wall.AddComponent<BoxCollider>();
    auto& platform = CreateBall(scene, {3.0f, 5.0f, 0.0f});
    platform.GetComponent<Rigidbody>()->SetKinematic(true);
    auto& balloon = CreateBall(scene, {6.0f, 5.0f, 0.0f});
    balloon.GetComponent<Rigidbody>()->SetGravityScale(0.0f);

    Simulate(scene, 1.0f);

    CHECK(GetHeight(wall) == 5.0f);
    CHECK(GetHeight(platform) == 5.0f);
    CHECK(GetHeight(balloon) == Approx(5.0f));
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 3);
}

TEST_CASE("The gravity of a world can be changed", "[physics]") {
    Scene scene;
    auto& ball = CreateBall(scene, {});
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetGravity() == PhysicsWorld::DefaultGravity);

    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({});
    Simulate(scene, 0.5f);
    CHECK(ball.GetTransform().GetPosition() == Vector3{});

    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({4.0f, 0.0f, 0.0f});
    Simulate(scene, 1.0f);
    CHECK(ball.GetTransform().GetPosition().x == Approx(2.0f).margin(0.1f));
    CHECK(GetHeight(ball) == Approx(0.0f).margin(0.001f));
}

TEST_CASE("Dynamic bodies come to rest on colliders", "[physics]") {
    Scene scene;
    CreateGround(scene);

    SECTION("Sphere") {
        auto& ball = CreateBall(scene, {0.0f, 3.0f, 0.0f});
        Simulate(scene, 3.0f);
        CHECK(GetHeight(ball) == Approx(0.5f).margin(RestMargin));
    }

    SECTION("Capsule") {
        auto& capsule = scene.CreateEntity("Capsule");
        capsule.GetTransform().SetPosition({0.0f, 3.0f, 0.0f});
        capsule.AddComponent<CapsuleCollider>(0.5f, 2.0f);
        capsule.AddComponent<Rigidbody>().SetAngularDamping(1.0f);
        Simulate(scene, 3.0f);
        // Standing on its cap, unless it tipped over.
        CHECK((GetHeight(capsule) == Approx(1.0f).margin(RestMargin) ||
               GetHeight(capsule) == Approx(0.5f).margin(RestMargin)));
    }

    SECTION("Box") {
        auto& crate = scene.CreateEntity("Crate");
        crate.GetTransform().SetPosition({0.0f, 3.0f, 0.0f});
        crate.AddComponent<BoxCollider>(Vector3{1.0f, 2.0f, 1.0f});
        crate.AddComponent<Rigidbody>();
        Simulate(scene, 3.0f);
        CHECK(GetHeight(crate) == Approx(1.0f).margin(RestMargin));
    }

    SECTION("Several colliders make one body") {
        auto& dumbbell = scene.CreateEntity("Dumbbell");
        dumbbell.GetTransform().SetPosition({0.0f, 3.0f, 0.0f});
        dumbbell.AddComponent<SphereCollider>().SetCenter({-1.0f, 0.0f, 0.0f});
        dumbbell.AddComponent<SphereCollider>().SetCenter({1.0f, 0.0f, 0.0f});
        dumbbell.AddComponent<Rigidbody>();
        Simulate(scene, 3.0f);
        CHECK(GetHeight(dumbbell) == Approx(0.5f).margin(RestMargin));
        CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 2);
    }
}

TEST_CASE("Colliders are scaled with their entity", "[physics]") {
    Scene scene;
    auto& ground = scene.CreateEntity("Ground");
    ground.GetTransform().SetScale({20.0f, 2.0f, 20.0f});
    ground.AddComponent<BoxCollider>();
    auto& ball = CreateBall(scene, {0.0f, 4.0f, 0.0f});
    ball.GetTransform().SetScale({2.0f, 2.0f, 2.0f});

    Simulate(scene, 3.0f);
    CHECK(GetHeight(ball) == Approx(2.0f).margin(RestMargin));

    ball.GetTransform().SetScale({1.0f, 1.0f, 1.0f});
    Simulate(scene, 2.0f);
    CHECK(GetHeight(ball) == Approx(1.5f).margin(RestMargin));
}

TEST_CASE("Bodies follow entities moved by the game", "[physics]") {
    Scene scene;
    auto& ground = CreateGround(scene);
    auto& ball = CreateBall(scene, {0.0f, 1.0f, 0.0f});
    Simulate(scene, 2.0f);
    REQUIRE(GetHeight(ball) == Approx(0.5f).margin(RestMargin));

    SECTION("A static body moved away lets what rested on it fall") {
        ground.GetTransform().SetPosition({0.0f, -5.5f, 0.0f});
        Simulate(scene, 2.0f);
        CHECK(GetHeight(ball) == Approx(-4.5f).margin(RestMargin));
    }

    SECTION("A dynamic body moved is placed there") {
        ball.GetTransform().SetPosition({5.0f, 3.0f, 0.0f});
        scene.Update(Step);
        CHECK(ball.GetTransform().GetPosition().x == Approx(5.0f));
        CHECK(GetHeight(ball) < 3.0f);
        CHECK(GetHeight(ball) > 2.9f);
    }

    SECTION("A kinematic body pushes dynamic ones") {
        scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({});
        auto& pusher = scene.CreateEntity("Pusher");
        pusher.GetTransform().SetPosition({-3.0f, 0.5f, 0.0f});
        pusher.AddComponent<BoxCollider>();
        pusher.AddComponent<Rigidbody>().SetKinematic(true);
        for (int i = 0; i < 120; ++i) {
            pusher.GetTransform().Translate({4.0f * Step, 0.0f, 0.0f});
            scene.Update(Step);
        }
        CHECK(pusher.GetTransform().GetPosition().x == Approx(5.0f));
        CHECK(ball.GetTransform().GetPosition().x > 5.0f);
    }
}

TEST_CASE("Bodies leave the world with their colliders and entities", "[physics]") {
    Scene scene;
    auto& ground = CreateGround(scene);
    auto& ball = CreateBall(scene, {0.0f, 1.0f, 0.0f});
    Simulate(scene, 1.0f);
    REQUIRE(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 2);

    SECTION("Destroyed ground lets bodies fall") {
        ground.Destroy();
        Simulate(scene, 1.0f);
        CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 1);
        CHECK(GetHeight(ball) < -1.0f);
    }

    SECTION("Inactive entities have no body") {
        ground.SetActive(false);
        Simulate(scene, 0.5f);
        CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 1);
        ground.SetActive(true);
        scene.Update(Step);
        CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 2);
    }

    SECTION("A rigidbody without colliders keeps a body") {
        ball.GetComponent<SphereCollider>()->SetEnabled(false);
        Simulate(scene, 1.0f);
        CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 2);
        CHECK(GetHeight(ball) < -1.0f);
    }

    SECTION("Removing the rigidbody leaves a static body") {
        ball.RemoveComponent<Rigidbody>();
        ground.Destroy();
        Simulate(scene, 1.0f);
        CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 1);
        CHECK(GetHeight(ball) == Approx(0.5f).margin(RestMargin));
    }

    SECTION("Entities moved to another scene take their body along") {
        Scene other;
        ball.MoveToScene(other);
        scene.Update(Step);
        other.Update(Step);
        CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 1);
        CHECK(other.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 1);
        // Nothing holds it in the other scene.
        Simulate(other, 1.0f);
        CHECK(GetHeight(ball) < -1.0f);
    }
}

TEST_CASE("Rigidbodies take velocities, impulses and forces", "[physics]") {
    Scene scene;
    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({});
    auto& ball = CreateBall(scene, {});
    auto& rigidbody = *ball.GetComponent<Rigidbody>();
    rigidbody.SetMass(2.0f);

    SECTION("A velocity set before the first step is the starting velocity") {
        rigidbody.SetLinearVelocity({1.0f, 0.0f, 0.0f});
        CHECK(rigidbody.GetLinearVelocity() == Vector3{1.0f, 0.0f, 0.0f});
        Simulate(scene, 1.0f);
        CHECK(ball.GetTransform().GetPosition().x == Approx(1.0f).margin(0.01f));
    }

    SECTION("An impulse changes the velocity by the impulse divided by the mass") {
        scene.Update(Step);
        rigidbody.AddImpulse({4.0f, 0.0f, 0.0f});
        CHECK(rigidbody.GetLinearVelocity().x == Approx(2.0f));
        Simulate(scene, 1.0f);
        CHECK(ball.GetTransform().GetPosition().x == Approx(2.0f).margin(0.01f));
    }

    SECTION("A force applied every fixed update accelerates the body") {
        ball.AddComponent<Thruster>(Vector3{4.0f, 0.0f, 0.0f});
        Simulate(scene, 1.0f);
        CHECK(rigidbody.GetLinearVelocity().x == Approx(2.0f).margin(0.01f));
        CHECK(ball.GetTransform().GetPosition().x == Approx(1.0f).margin(0.05f));
    }

    SECTION("Angular velocity turns the body") {
        rigidbody.SetAngularDamping(0.0f);
        rigidbody.SetAngularVelocity({0.0f, 1.0f, 0.0f});
        Simulate(scene, 1.0f);
        CHECK(Spot::ToEuler(ball.GetTransform().GetRotation()).y == Approx(1.0f).margin(0.02f));
    }
}

TEST_CASE("Bodies spinning as fast as the simulation allows keep spinning", "[physics]") {
    Scene scene;
    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({});
    // Rotations do not keep this scale exactly, which must not rebuild the bodies as they turn, and the speed limit
    // must hold whatever the axis.
    std::vector<Rigidbody*> rigidbodies;
    for (int i = 0; i < 32; ++i) {
        const float angle = static_cast<float>(i) * 0.37f;
        auto& ball = CreateBall(scene, {static_cast<float>(i) * 2.0f, 0.0f, 0.0f});
        ball.GetTransform().SetScale({0.4f, 0.4f, 0.4f});
        auto& rigidbody = *ball.GetComponent<Rigidbody>();
        rigidbody.SetAngularDamping(0.0f);
        rigidbody.SetAngularVelocity(Vector3{std::cos(angle), std::sin(angle), 0.5f} * 1000.0f);
        rigidbodies.push_back(&rigidbody);
    }

    Simulate(scene, 2.0f);

    for (const Rigidbody* rigidbody : rigidbodies) {
        const float speed = Spot::Length(rigidbody->GetAngularVelocity());
        CHECK(speed > 1.0f);
        CHECK(speed < 1000.0f);
    }
}

TEST_CASE("Fixed updates run at the fixed time step", "[physics]") {
    Scene scene;
    scene.SetFixedTimeStep(0.25f);
    auto& counter = scene.CreateEntity("Counter").AddComponent<FixedUpdateCounter>();

    scene.Update(1.0f);
    CHECK(counter.count == 4);
    CHECK(counter.lastDeltaTime == 0.25f);

    scene.Update(0.125f);
    CHECK(counter.count == 4);
    scene.Update(0.125f);
    CHECK(counter.count == 5);

    // A long frame runs a limited number of steps, and the rest of its time is dropped.
    scene.Update(10.0f);
    CHECK(counter.count == 5 + Scene::MaxFixedStepsPerUpdate);
    scene.Update(0.125f);
    CHECK(counter.count == 5 + Scene::MaxFixedStepsPerUpdate);
}

TEST_CASE("Colliders and rigidbodies are saved with their scene", "[physics]") {
    Scene scene;
    auto& entity = scene.CreateEntity("Body");
    auto& box = entity.AddComponent<BoxCollider>(Vector3{1.0f, 2.0f, 3.0f});
    box.SetCenter({0.0f, 1.0f, 0.0f});
    box.SetFriction(0.25f);
    box.SetRestitution(0.75f);
    entity.AddComponent<SphereCollider>(2.0f);
    entity.AddComponent<CapsuleCollider>(0.25f, 1.5f);
    auto& rigidbody = entity.AddComponent<Rigidbody>();
    rigidbody.SetMass(3.0f);
    rigidbody.SetKinematic(true);
    rigidbody.SetGravityScale(0.5f);
    rigidbody.SetLinearDamping(0.1f);
    rigidbody.SetAngularDamping(0.2f);

    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const Entity* copy = loaded.FindEntity("Body");
    REQUIRE(copy);

    const auto* loadedBox = copy->GetComponent<BoxCollider>();
    REQUIRE(loadedBox);
    CHECK(loadedBox->GetSize() == Vector3{1.0f, 2.0f, 3.0f});
    CHECK(loadedBox->GetCenter() == Vector3{0.0f, 1.0f, 0.0f});
    CHECK(loadedBox->GetFriction() == 0.25f);
    CHECK(loadedBox->GetRestitution() == 0.75f);
    REQUIRE(copy->GetComponent<SphereCollider>());
    CHECK(copy->GetComponent<SphereCollider>()->GetRadius() == 2.0f);
    REQUIRE(copy->GetComponent<CapsuleCollider>());
    CHECK(copy->GetComponent<CapsuleCollider>()->GetRadius() == 0.25f);
    CHECK(copy->GetComponent<CapsuleCollider>()->GetHeight() == 1.5f);

    const auto* loadedRigidbody = copy->GetComponent<Rigidbody>();
    REQUIRE(loadedRigidbody);
    CHECK(loadedRigidbody->GetMass() == 3.0f);
    CHECK(loadedRigidbody->IsKinematic());
    CHECK(loadedRigidbody->GetGravityScale() == 0.5f);
    CHECK(loadedRigidbody->GetLinearDamping() == 0.1f);
    CHECK(loadedRigidbody->GetAngularDamping() == 0.2f);
}

TEST_CASE("Entities have a layer, saved with their scene", "[physics][layers]") {
    Scene scene;
    Entity& entity = scene.CreateEntity("Layered");
    CHECK(entity.GetLayer() == 0);
    entity.SetLayer(7);
    CHECK(entity.GetLayer() == 7);

    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    REQUIRE(loaded.FindEntity("Layered"));
    CHECK(loaded.FindEntity("Layered")->GetLayer() == 7);
}

TEST_CASE("Bodies on layers that do not collide pass through each other", "[physics][layers]") {
    Scene scene;
    CreateGround(scene);
    Entity& ghost = CreateBall(scene, {0.0f, 1.0f, 0.0f});
    ghost.SetLayer(3);
    Entity& ball = CreateBall(scene, {2.0f, 1.0f, 0.0f});
    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetLayerCollision(0, 3, false);
    CHECK_FALSE(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetLayerCollision(3, 0));
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetLayerCollision(3, 3));

    Simulate(scene, 1.0f);
    CHECK(GetHeight(ball) == Approx(0.5f).margin(RestMargin));
    CHECK(GetHeight(ghost) < -1.0f);

    // Moving the ball to the ghost's layer drops it through the ground too.
    ball.SetLayer(3);
    Simulate(scene, 1.0f);
    CHECK(GetHeight(ball) < -1.0f);
}

TEST_CASE("Physics settings keep layer names and a symmetric collision matrix", "[physics][layers]") {
    Spot::PhysicsSettings settings;
    CHECK(settings.FindLayer("Default") == 0);
    CHECK_FALSE(settings.FindLayer("Props").has_value());
    CHECK_FALSE(settings.FindLayer("").has_value());
    settings.layerNames[4] = "Props";
    settings.SetCollides(4, 0, false);
    CHECK_FALSE(settings.Collides(0, 4));
    CHECK(settings.Collides(4, 4));

    // A matrix that is not symmetric, as edited by hand, becomes symmetric: pairs collide only when both say so.
    Spot::SettingsSections sections;
    sections.physics = settings;
    std::string text = sections.SaveToString();
    Spot::SettingsSections loaded;
    REQUIRE(loaded.LoadFromString(text));
    CHECK(loaded.physics.FindLayer("Props") == 4);
    CHECK_FALSE(loaded.physics.Collides(4, 0));
    loaded.physics.layerCollisions[1] &= ~(1u << 2);
    REQUIRE(loaded.LoadFromString(loaded.SaveToString()));
    CHECK_FALSE(loaded.physics.Collides(2, 1));

    // Applying them gives the scene's physics the names and the matrix.
    Scene scene;
    loaded.Apply(scene);
    REQUIRE(scene.GetSystem<PhysicsWorld>() != nullptr);
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().FindLayer("Props") == 4);
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetLayerName(4) == "Props");
    CHECK_FALSE(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetLayerCollision(0, 4));
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetLayerCollision(4, 4));

    // Default settings leave physics to be created when needed.
    Scene empty;
    Spot::SettingsSections{}.Apply(empty);
    CHECK(empty.GetSystem<PhysicsWorld>() == nullptr);
    CHECK(Spot::PhysicsSettings{}.gravity == PhysicsWorld::DefaultGravity);
    CHECK(Spot::PhysicsSettings{}.fixedTimeStep == Scene::DefaultFixedTimeStep);
}

TEST_CASE("Triggers let bodies through, as sensor bodies of their own", "[physics][triggers]") {
    Scene scene;
    CreateGround(scene);
    auto& zone = scene.CreateEntity("Zone");
    zone.GetTransform().SetPosition({0.0f, 2.0f, 0.0f});
    zone.AddComponent<BoxCollider>(Vector3{4.0f, 1.0f, 4.0f}).SetTrigger(true);
    Entity& ball = CreateBall(scene, {0.0f, 4.0f, 0.0f});

    Simulate(scene, 2.0f);
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 3);
    // It fell through the trigger onto the ground.
    CHECK(GetHeight(ball) == Approx(0.5f).margin(RestMargin));

    // An entity with solid and trigger colliders has a body of each.
    auto& both = scene.CreateEntity("Both");
    both.GetTransform().SetPosition({5.0f, 0.5f, 0.0f});
    both.AddComponent<BoxCollider>();
    both.AddComponent<SphereCollider>().SetTrigger(true);
    both.AddComponent<Rigidbody>();
    Simulate(scene, 0.5f);
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 5);
    CHECK(GetHeight(both) == Approx(0.5f).margin(RestMargin));

    // A trigger becoming solid holds what falls on it.
    zone.GetComponent<BoxCollider>()->SetTrigger(false);
    Entity& second = CreateBall(scene, {0.0f, 4.0f, 0.0f});
    Simulate(scene, 2.0f);
    CHECK(GetHeight(second) == Approx(3.0f).margin(RestMargin));
}

TEST_CASE("Triggers are saved with their scene", "[physics][triggers]") {
    Scene scene;
    scene.CreateEntity("Zone").AddComponent<SphereCollider>().SetTrigger(true);
    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const auto* trigger = loaded.FindComponent<SphereCollider>();
    REQUIRE(trigger);
    CHECK(trigger->IsTrigger());
}

TEST_CASE("Forces and impulses at a point turn bodies, and torques spin them", "[physics][rigidbody]") {
    Scene scene;
    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({});
    const auto create = [&scene](Vector3 position) -> Rigidbody& {
        auto& entity = scene.CreateEntity();
        entity.GetTransform().SetPosition(position);
        entity.AddComponent<BoxCollider>();
        return entity.AddComponent<Rigidbody>();
    };
    Rigidbody& centered = create({0.0f, 0.0f, 0.0f});
    Rigidbody& offCenter = create({5.0f, 0.0f, 0.0f});
    Rigidbody& twisted = create({10.0f, 0.0f, 0.0f});
    Rigidbody& pushed = create({15.0f, 0.0f, 0.0f});
    scene.Update(Step);

    centered.AddImpulseAtPosition({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f});
    offCenter.AddImpulseAtPosition({0.0f, 0.0f, 1.0f}, {5.5f, 0.0f, 0.0f});
    twisted.AddTorque({0.0f, 50.0f, 0.0f});
    pushed.AddForceAtPosition({0.0f, 0.0f, 50.0f}, {15.5f, 0.0f, 0.0f});
    scene.Update(Step);

    CHECK(centered.GetLinearVelocity().z == Approx(1.0f).margin(0.01f));
    CHECK(Spot::Length(centered.GetAngularVelocity()) == Approx(0.0f).margin(1e-4f));
    CHECK(offCenter.GetLinearVelocity().z == Approx(1.0f).margin(0.01f));
    // Pushed forward at its right side, it turns to the left, around -Y.
    CHECK(offCenter.GetAngularVelocity().y < -0.5f);
    CHECK(twisted.GetAngularVelocity().y > 0.1f);
    CHECK(pushed.GetLinearVelocity().z > 0.0f);
    CHECK(pushed.GetAngularVelocity().y < 0.0f);
}

TEST_CASE("Locked rotations keep a body from turning", "[physics][rigidbody]") {
    Scene scene;
    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({});
    auto& entity = scene.CreateEntity();
    entity.AddComponent<BoxCollider>();
    auto& rigidbody = entity.AddComponent<Rigidbody>();
    rigidbody.SetRotationLocks({.x = true, .y = true, .z = true});
    scene.Update(Step);
    rigidbody.AddImpulseAtPosition({0.0f, 0.0f, 1.0f}, {0.5f, 0.0f, 0.0f});
    Simulate(scene, 0.5f);
    CHECK(Spot::Length(rigidbody.GetAngularVelocity()) == Approx(0.0f).margin(1e-4f));
    CHECK(entity.GetTransform().GetRotation() == Spot::Quaternion{});
    CHECK(rigidbody.GetLinearVelocity().z == Approx(1.0f).margin(0.01f));

    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    CHECK(loaded.FindComponent<Rigidbody>()->GetRotationLocks() == Spot::RotationLocks{true, true, true});
}

TEST_CASE("Continuous bodies cannot pass through thin ones however fast they go", "[physics][rigidbody]") {
    const auto shoot = [](bool continuous) {
        Scene scene;
        scene.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({});
        auto& wall = scene.CreateEntity("Wall");
        wall.GetTransform().SetPosition({0.0f, 0.0f, -5.0f});
        wall.AddComponent<BoxCollider>(Vector3{4.0f, 4.0f, 0.05f});
        auto& bullet = scene.CreateEntity("Bullet");
        bullet.GetTransform().SetScale({0.1f, 0.1f, 0.1f});
        bullet.AddComponent<SphereCollider>();
        auto& body = bullet.AddComponent<Rigidbody>();
        body.SetContinuous(continuous);
        body.SetLinearVelocity({0.0f, 0.0f, -400.0f});
        Simulate(scene, 0.2f);
        return bullet.GetTransform().GetPosition().z;
    };
    CHECK(shoot(false) < -10.0f);
    CHECK(shoot(true) > -5.1f);
}

TEST_CASE("Rigidbodies sleep when told, wake up when told, and may never sleep", "[physics][rigidbody]") {
    Scene scene;
    CreateGround(scene);
    Entity& ball = CreateBall(scene, {0.0f, 0.5f, 0.0f});
    Entity& restless = CreateBall(scene, {3.0f, 0.5f, 0.0f});
    auto& body = *ball.GetComponent<Rigidbody>();
    restless.GetComponent<Rigidbody>()->SetCanSleep(false);
    scene.Update(Step);
    CHECK_FALSE(body.IsSleeping());

    body.Sleep();
    CHECK(body.IsSleeping());
    body.WakeUp();
    CHECK_FALSE(body.IsSleeping());

    Simulate(scene, 3.0f);
    CHECK(body.IsSleeping());
    CHECK_FALSE(restless.GetComponent<Rigidbody>()->IsSleeping());
}

namespace {

std::shared_ptr<const Spot::MeshData> MakePlaneMesh(float size) {
    return std::make_shared<const Spot::MeshData>(Spot::MeshData::CreatePlane(size));
}

} // namespace

TEST_CASE("Mesh colliders hold bodies on their triangles", "[physics][mesh]") {
    Scene scene;
    auto& ground = scene.CreateEntity("Ground");
    auto& collider = ground.AddComponent<Spot::MeshCollider>();
    collider.SetMesh(MakePlaneMesh(20.0f));
    Entity& ball = CreateBall(scene, {0.0f, 2.0f, 0.0f});
    Simulate(scene, 2.0f);
    CHECK(GetHeight(ball) == Approx(0.5f).margin(RestMargin));

    const auto hit = scene.GetOrAddSystem<Spot::PhysicsWorld>().Raycast({3.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->collider == &collider);
    CHECK(hit->normal.y == Approx(1.0f).margin(1e-3f));

    // The shape scales with the entity.
    CHECK_FALSE(scene.GetOrAddSystem<Spot::PhysicsWorld>()
                    .Raycast({15.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f)
                    .has_value());
    ground.GetTransform().SetScale({2.0f, 1.0f, 2.0f});
    Simulate(scene, Step);
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>()
              .Raycast({15.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f)
              .has_value());
}

TEST_CASE("Mesh colliders of tilted meshes make slopes", "[physics][mesh]") {
    Scene scene;
    CreateGround(scene);
    auto& slope = scene.CreateEntity("Slope");
    slope.GetTransform().SetPosition({0.0f, 2.0f, 0.0f});
    slope.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, 0.5f));
    slope.AddComponent<Spot::MeshCollider>().SetMesh(MakePlaneMesh(6.0f));
    Entity& ball = CreateBall(scene, {0.0f, 4.0f, 0.0f});
    Simulate(scene, 1.0f);
    // Turned around Z by a positive angle, the slope goes down toward -X.
    CHECK(ball.GetTransform().GetPosition().x < -0.5f);
}

TEST_CASE("Mesh colliders of dynamic bodies take the convex hull", "[physics][mesh]") {
    Scene scene;
    CreateGround(scene);
    auto& crate = scene.CreateEntity("Crate");
    crate.GetTransform().SetPosition({0.0f, 3.0f, 0.0f});
    auto& collider = crate.AddComponent<Spot::MeshCollider>();
    collider.SetMesh(std::make_shared<const Spot::MeshData>(Spot::MeshData::CreateCube()));
    CHECK_FALSE(collider.IsConvex());
    crate.AddComponent<Rigidbody>();
    Simulate(scene, 2.0f);
    CHECK(GetHeight(crate) == Approx(0.5f).margin(RestMargin));

    // Meshes shared by many colliders, convex or not, are fine.
    auto mesh = std::make_shared<const Spot::MeshData>(Spot::MeshData::CreateSphere(0.5f));
    for (int i = 0; i < 20; ++i) {
        auto& ball = scene.CreateEntity("Ball");
        ball.GetTransform().SetPosition({static_cast<float>(i) * 1.5f - 15.0f, 2.0f, 4.0f});
        auto& sphere = ball.AddComponent<Spot::MeshCollider>();
        sphere.SetMesh(mesh);
        sphere.SetConvex(true);
        ball.AddComponent<Rigidbody>();
    }
    Simulate(scene, 2.0f);
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 22);
}

TEST_CASE("Mesh colliders save their mesh asset, and assets give meshes' triangles without a GPU", "[physics][mesh]") {
    Scene scene;
    scene.CreateEntity("Holder").AddComponent<Spot::MeshCollider>().SetMesh(
        Spot::AssetRef<Spot::MeshData>(Spot::BuiltInAssets::CubeMesh, nullptr));
    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    REQUIRE(loaded.FindComponent<Spot::MeshCollider>());
    CHECK(loaded.FindComponent<Spot::MeshCollider>()->GetMesh().GetId() == Spot::BuiltInAssets::CubeMesh);

    Spot::Tests::RegisteredAssetManager assets("missing-imports");
    const auto cube = assets.LoadMeshData(Spot::BuiltInAssets::CubeMesh);
    REQUIRE(cube);
    CHECK(cube->vertices.size() == 24);
    CHECK(assets.Load<Spot::MeshData>(Spot::BuiltInAssets::CubeMesh).GetShared() == cube);
}
