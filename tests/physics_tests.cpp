#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

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

constexpr float Step = PhysicsWorld::DefaultFixedTimeStep;
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
    CHECK(scene.GetPhysics().GetBodyCount() == 3);
}

TEST_CASE("The gravity of a world can be changed", "[physics]") {
    Scene scene;
    auto& ball = CreateBall(scene, {});
    CHECK(scene.GetPhysics().GetGravity() == PhysicsWorld::DefaultGravity);

    scene.GetPhysics().SetGravity({});
    Simulate(scene, 0.5f);
    CHECK(ball.GetTransform().GetPosition() == Vector3{});

    scene.GetPhysics().SetGravity({4.0f, 0.0f, 0.0f});
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
        CHECK(scene.GetPhysics().GetBodyCount() == 2);
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
        scene.GetPhysics().SetGravity({});
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
    REQUIRE(scene.GetPhysics().GetBodyCount() == 2);

    SECTION("Destroyed ground lets bodies fall") {
        ground.Destroy();
        Simulate(scene, 1.0f);
        CHECK(scene.GetPhysics().GetBodyCount() == 1);
        CHECK(GetHeight(ball) < -1.0f);
    }

    SECTION("Inactive entities have no body") {
        ground.SetActive(false);
        Simulate(scene, 0.5f);
        CHECK(scene.GetPhysics().GetBodyCount() == 1);
        ground.SetActive(true);
        scene.Update(Step);
        CHECK(scene.GetPhysics().GetBodyCount() == 2);
    }

    SECTION("A rigidbody without colliders keeps a body") {
        ball.GetComponent<SphereCollider>()->SetEnabled(false);
        Simulate(scene, 1.0f);
        CHECK(scene.GetPhysics().GetBodyCount() == 2);
        CHECK(GetHeight(ball) < -1.0f);
    }

    SECTION("Removing the rigidbody leaves a static body") {
        ball.RemoveComponent<Rigidbody>();
        ground.Destroy();
        Simulate(scene, 1.0f);
        CHECK(scene.GetPhysics().GetBodyCount() == 1);
        CHECK(GetHeight(ball) == Approx(0.5f).margin(RestMargin));
    }

    SECTION("Entities moved to another scene take their body along") {
        Scene other;
        ball.MoveToScene(other);
        scene.Update(Step);
        other.Update(Step);
        CHECK(scene.GetPhysics().GetBodyCount() == 1);
        CHECK(other.GetPhysics().GetBodyCount() == 1);
        // Nothing holds it in the other scene.
        Simulate(other, 1.0f);
        CHECK(GetHeight(ball) < -1.0f);
    }
}

TEST_CASE("Rigidbodies take velocities, impulses and forces", "[physics]") {
    Scene scene;
    scene.GetPhysics().SetGravity({});
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

TEST_CASE("Fixed updates run at the fixed time step", "[physics]") {
    Scene scene;
    scene.GetPhysics().SetFixedTimeStep(0.25f);
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
    CHECK(counter.count == 5 + PhysicsWorld::MaxStepsPerUpdate);
    scene.Update(0.125f);
    CHECK(counter.count == 5 + PhysicsWorld::MaxStepsPerUpdate);
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
