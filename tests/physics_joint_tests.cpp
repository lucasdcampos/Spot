#include <spot.h>
#include <spot/scene/physics/joint_break_listener.h>
#include <spot/scene/physics/physics_world.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using Spot::BoxCollider;
using Spot::Entity;
using Spot::Rigidbody;
using Spot::Scene;
using Spot::SphereCollider;
using Spot::Vector3;

namespace {

constexpr float Step = Scene::DefaultFixedTimeStep;

void Simulate(Scene& scene, float seconds) {
    const auto updates = std::lround(seconds / Step);
    for (long i = 0; i < updates; ++i) {
        scene.Update(Step);
    }
}

Entity& CreateBox(Scene& scene, const char* name, Vector3 position, float mass = 1.0f) {
    auto& entity = scene.CreateEntity(name);
    entity.GetTransform().SetPosition(position);
    entity.AddComponent<BoxCollider>(Vector3{0.5f, 0.5f, 0.5f});
    entity.AddComponent<Rigidbody>().SetMass(mass);
    return entity;
}

Vector3 PositionOf(const Entity& entity) {
    return entity.GetTransform().GetPosition();
}

class BreakListener final : public Spot::Component, public Spot::JointBreakListener {
public:
    int breaks = 0;

protected:
    void OnJointBreak(const Spot::Joint& /*joint*/) override { ++breaks; }
};

} // namespace

TEST_CASE("Fixed joints hold a body to the world or to another body", "[physics][joints]") {
    Scene scene;
    Entity& held = CreateBox(scene, "Held", {0.0f, 5.0f, 0.0f});
    held.AddComponent<Spot::FixedJoint>();
    Entity& first = CreateBox(scene, "First", {3.0f, 5.0f, 0.0f});
    Entity& second = CreateBox(scene, "Second", {3.5f, 5.0f, 0.0f});
    second.AddComponent<Spot::FixedJoint>().SetConnectedEntity(first.GetId());

    Simulate(scene, 1.0f);
    CHECK(PositionOf(held).y == Approx(5.0f).margin(0.01f));
    // The pair fell together, keeping its shape.
    CHECK(PositionOf(first).y < 1.0f);
    CHECK(Spot::Distance(PositionOf(first), PositionOf(second)) == Approx(0.5f).margin(0.01f));
}

TEST_CASE("Hinge joints swing within their limits, and their motors turn them", "[physics][joints]") {
    Scene scene;
    // A pendulum hanging from a hinge along Z, one meter above it, started sideways.
    Entity& pendulum = CreateBox(scene, "Pendulum", {1.0f, 5.0f, 0.0f});
    auto& hinge = pendulum.AddComponent<Spot::HingeJoint>();
    hinge.SetAnchor({-1.0f, 0.0f, 0.0f});
    hinge.SetAxis({0.0f, 0.0f, 1.0f});
    Simulate(scene, 0.5f);
    // It swings down around the anchor at (0, 5), keeping its length.
    CHECK(PositionOf(pendulum).y < 4.5f);
    CHECK(Spot::Distance(PositionOf(pendulum), Vector3{0.0f, 5.0f, 0.0f}) == Approx(1.0f).margin(0.02f));
    CHECK(PositionOf(pendulum).z == Approx(0.0f).margin(0.01f));

    Scene limited;
    Entity& door = CreateBox(limited, "Door", {1.0f, 5.0f, 0.0f});
    auto& stop = door.AddComponent<Spot::HingeJoint>();
    stop.SetAnchor({-1.0f, 0.0f, 0.0f});
    stop.SetAxis({0.0f, 0.0f, 1.0f});
    stop.SetLimits(true, -0.3f, 0.3f);
    Simulate(limited, 1.0f);
    CHECK(stop.GetAngle() == Approx(-0.3f).margin(0.05f));
    CHECK(PositionOf(door).y > 4.6f);

    Scene driven;
    driven.GetOrAddSystem<Spot::PhysicsWorld>().SetGravity({});
    Entity& wheel = CreateBox(driven, "Wheel", {0.0f, 1.0f, 0.0f});
    auto& axle = wheel.AddComponent<Spot::HingeJoint>();
    axle.SetAxis({1.0f, 0.0f, 0.0f});
    driven.Update(Step);
    axle.SetMotor(true, 2.0f, 50.0f);
    Simulate(driven, 0.5f);
    CHECK(wheel.GetComponent<Rigidbody>()->GetAngularVelocity().x == Approx(2.0f).margin(0.1f));
    CHECK(axle.GetAngle() > 0.5f);
}

TEST_CASE("Distance and spring joints hang bodies from a point", "[physics][joints]") {
    Scene scene;
    // A rope two meters long from a point in the world, the ball starting a meter below it.
    auto& ball = scene.CreateEntity("Ball");
    ball.GetTransform().SetPosition({0.0f, 4.0f, 0.0f});
    ball.AddComponent<SphereCollider>(0.2f);
    ball.AddComponent<Rigidbody>();
    auto& rope = ball.AddComponent<Spot::DistanceJoint>();
    rope.SetConnectedAnchor({0.0f, 5.0f, 0.0f});
    rope.SetDistance(0.0f, 2.0f);

    auto& weight = scene.CreateEntity("Weight");
    weight.GetTransform().SetPosition({5.0f, 4.0f, 0.0f});
    weight.AddComponent<SphereCollider>(0.2f);
    weight.AddComponent<Rigidbody>();
    auto& spring = weight.AddComponent<Spot::SpringJoint>();
    spring.SetConnectedAnchor({5.0f, 5.0f, 0.0f});
    spring.SetRestLength(1.0f);
    spring.SetDamping(1.0f);

    Simulate(scene, 3.0f);
    CHECK(PositionOf(ball).y == Approx(3.0f).margin(0.02f));
    // Gravity stretches the spring beyond its rest length, and it settles.
    CHECK(PositionOf(weight).y < 4.0f);
    CHECK(PositionOf(weight).y > 2.0f);
    CHECK(std::abs(weight.GetComponent<Rigidbody>()->GetLinearVelocity().y) < 0.1f);
}

TEST_CASE("Swing-twist joints keep their axis within its cone", "[physics][joints]") {
    Scene scene;
    // A bone hanging down a meter below its joint, along its twist axis, pushed sideways.
    Entity& bone = CreateBox(scene, "Bone", {0.0f, 4.0f, 0.0f});
    auto& joint = bone.AddComponent<Spot::SwingTwistJoint>();
    joint.SetAnchor({0.0f, 1.0f, 0.0f});
    joint.SetTwistAxis({0.0f, 1.0f, 0.0f});
    joint.SetSwingLimit(0.2f);
    scene.Update(Step);
    bone.GetComponent<Rigidbody>()->AddImpulse({5.0f, 0.0f, 0.0f});
    Simulate(scene, 1.0f);
    // Within 0.2 radians of hanging straight down, it stays near the vertical below the joint.
    CHECK(std::abs(PositionOf(bone).x) < std::sin(0.2f) + 0.05f);
    CHECK(Spot::Distance(PositionOf(bone), Vector3{0.0f, 5.0f, 0.0f}) == Approx(1.0f).margin(0.02f));
}

TEST_CASE("Joints break past their break force, and outlive the rebuilding of their bodies", "[physics][joints]") {
    Scene scene;
    Entity& heavy = CreateBox(scene, "Heavy", {0.0f, 5.0f, 0.0f}, 100.0f);
    auto& weld = heavy.AddComponent<Spot::FixedJoint>();
    weld.SetBreakForce(500.0f);
    auto& listener = heavy.AddComponent<BreakListener>();
    Entity& light = CreateBox(scene, "Light", {3.0f, 5.0f, 0.0f}, 1.0f);
    auto& strong = light.AddComponent<Spot::FixedJoint>();
    strong.SetBreakForce(500.0f);

    Simulate(scene, 0.5f);
    // A hundred kilograms weigh 981 newtons, more than the weld holds; one kilogram does not.
    CHECK(weld.IsBroken());
    CHECK_FALSE(weld.IsEnabled());
    CHECK(listener.breaks == 1);
    CHECK(PositionOf(heavy).y < 4.5f);
    CHECK_FALSE(strong.IsBroken());
    CHECK(strong.GetForce() == Approx(9.81f).margin(1.0f));

    // A collider change rebuilds the body, and the joint is made again with it.
    light.GetComponent<BoxCollider>()->SetSize({0.6f, 0.6f, 0.6f});
    Simulate(scene, 0.5f);
    CHECK(PositionOf(light).y == Approx(5.0f).margin(0.02f));

    // Enabling a broken joint mends it, where its body is now.
    weld.SetEnabled(true);
    CHECK_FALSE(weld.IsBroken());
}

TEST_CASE("Joints are saved with their scene, keeping what they connect", "[physics][joints]") {
    Scene scene;
    Entity& frame = CreateBox(scene, "Frame", {0.0f, 0.0f, 0.0f});
    Entity& door = CreateBox(scene, "Door", {1.0f, 0.0f, 0.0f});
    auto& hinge = door.AddComponent<Spot::HingeJoint>();
    hinge.SetConnectedEntity(frame.GetId());
    hinge.SetLimits(true, -1.0f, 1.0f);

    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const auto* copy = loaded.FindComponent<Spot::HingeJoint>();
    REQUIRE(copy);
    CHECK(copy->GetConnectedEntity() == loaded.FindEntity("Frame")->GetId());
    CHECK(copy->HasLimits());
    CHECK(copy->GetMaxAngle() == 1.0f);
}
