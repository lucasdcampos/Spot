#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

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

// Records the physics events of its entity as "<event> <other's name>".
class EventRecorder final : public Spot::Component {
public:
    std::vector<std::string> events;
    std::vector<Spot::Collision> enters;
    std::function<void(const Spot::Collision&)> onEnter;
    int stays = 0;

    bool Has(const std::string& event) const { return std::ranges::find(events, event) != events.end(); }
    auto Count(const std::string& event) const { return std::ranges::count(events, event); }

protected:
    void OnCollisionEnter(const Spot::Collision& collision) override {
        events.push_back("enter " + Name(collision.other));
        enters.push_back(collision);
        if (const auto run = onEnter) {
            run(collision);
        }
    }
    void OnCollisionStay(const Spot::Collision& /*collision*/) override { ++stays; }
    void OnCollisionExit(const Spot::Collision& collision) override {
        events.push_back("exit " + Name(collision.other));
    }
    void OnTriggerEnter(const Spot::TriggerContact& contact) override {
        events.push_back(std::string(contact.isTrigger ? "entered by " : "trigger enter ") + Name(contact.other));
    }
    void OnTriggerExit(const Spot::TriggerContact& contact) override {
        events.push_back(std::string(contact.isTrigger ? "left by " : "trigger exit ") + Name(contact.other));
    }

private:
    std::string Name(Spot::EntityId id) const {
        const Entity* entity = GetScene().GetEntity(id);
        return entity ? entity->GetName() : "(gone)";
    }
};

Entity& CreateGround(Scene& scene) {
    auto& ground = scene.CreateEntity("Ground");
    ground.GetTransform().SetPosition({0.0f, -0.5f, 0.0f});
    ground.AddComponent<BoxCollider>(Vector3{20.0f, 1.0f, 20.0f});
    return ground;
}

Entity& CreateBall(Scene& scene, Vector3 position, const std::string& name = "Ball") {
    auto& ball = scene.CreateEntity(name);
    ball.GetTransform().SetPosition(position);
    ball.AddComponent<SphereCollider>();
    ball.AddComponent<Rigidbody>();
    return ball;
}

} // namespace

TEST_CASE("Bodies that meet get an enter, stays while awake, and an exit once apart", "[physics][events]") {
    Scene scene;
    auto& ground = CreateGround(scene).AddComponent<EventRecorder>();
    Entity& ball = CreateBall(scene, {0.0f, 2.0f, 0.0f});
    auto& recorder = ball.AddComponent<EventRecorder>();

    Simulate(scene, 3.0f);
    CHECK(recorder.events == std::vector<std::string>{"enter Ground"});
    CHECK(ground.events == std::vector<std::string>{"enter Ball"});
    CHECK(recorder.stays > 0);
    REQUIRE(recorder.enters.size() == 1);
    const Spot::Collision& hit = recorder.enters[0];
    // The ball is pushed up, and it was falling onto the ground when they met.
    CHECK(hit.normal.y == Approx(1.0f).margin(0.01f));
    CHECK(hit.relativeVelocity.y < -1.0f);
    CHECK(hit.impulse > 0.0f);
    CHECK(hit.pointCount >= 1);
    CHECK(hit.GetPoints().front().y == Approx(0.0f).margin(0.05f));
    // The ground sees the same contact from its side.
    CHECK(ground.enters[0].normal.y == Approx(-1.0f).margin(0.01f));
    CHECK(ground.enters[0].relativeVelocity.y > 1.0f);

    // Resting bodies fall asleep: they keep touching, without stays.
    const int stays = recorder.stays;
    Simulate(scene, 1.0f);
    CHECK(recorder.stays == stays);
    CHECK(recorder.events.size() == 1);

    ball.GetTransform().SetPosition({0.0f, 5.0f, 0.0f});
    Simulate(scene, 0.1f);
    CHECK(recorder.events == std::vector<std::string>{"enter Ground", "exit Ground"});
    CHECK(ground.events == std::vector<std::string>{"enter Ball", "exit Ball"});
}

TEST_CASE("Triggers tell both entities when a body enters and leaves them", "[physics][events]") {
    Scene scene;
    auto& zone = scene.CreateEntity("Zone");
    zone.GetTransform().SetPosition({0.0f, 5.0f, 0.0f});
    zone.AddComponent<BoxCollider>(Vector3{4.0f, 1.0f, 4.0f}).SetTrigger(true);
    auto& zoneEvents = zone.AddComponent<EventRecorder>();
    Entity& ball = CreateBall(scene, {0.0f, 8.0f, 0.0f});
    auto& ballEvents = ball.AddComponent<EventRecorder>();

    Simulate(scene, 2.0f);
    CHECK(zoneEvents.events == std::vector<std::string>{"entered by Ball", "left by Ball"});
    CHECK(ballEvents.events == std::vector<std::string>{"trigger enter Zone", "trigger exit Zone"});
}

TEST_CASE("Entities destroyed from inside a collision hook end their contacts", "[physics][events]") {
    Scene scene;
    auto& ground = CreateGround(scene).AddComponent<EventRecorder>();
    Entity& ball = CreateBall(scene, {0.0f, 1.0f, 0.0f});
    auto& ballEvents = ball.AddComponent<EventRecorder>();
    const Spot::EntityId ballId = ball.GetId();
    ground.onEnter = [&scene](const Spot::Collision& collision) {
        if (Entity* other = scene.GetEntity(collision.other)) {
            other->Destroy();
        }
    };
    // The ball's own hook runs or not, depending on the order, but nothing breaks either way.
    ballEvents.onEnter = [](const Spot::Collision&) {};

    Simulate(scene, 1.0f);
    CHECK(scene.GetEntity(ballId) == nullptr);
    CHECK(ground.events == std::vector<std::string>{"enter Ball", "exit (gone)"});
    CHECK(scene.GetPhysics().GetBodyCount() == 1);
}

TEST_CASE("Disabling or removing a collider ends its contacts", "[physics][events]") {
    Scene scene;
    auto& ground = CreateGround(scene).AddComponent<EventRecorder>();
    Entity& first = CreateBall(scene, {0.0f, 0.6f, 0.0f}, "First");
    Entity& second = CreateBall(scene, {3.0f, 0.6f, 0.0f}, "Second");
    Simulate(scene, 1.0f);
    CHECK(ground.Count("enter First") == 1);
    CHECK(ground.Count("enter Second") == 1);

    first.GetComponent<SphereCollider>()->SetEnabled(false);
    second.RemoveComponent<SphereCollider>();
    Simulate(scene, 0.1f);
    CHECK(ground.Has("exit First"));
    CHECK(ground.Has("exit Second"));
}

TEST_CASE("Collision hooks that throw fault only their component", "[physics][events]") {
    class Throwing final : public Spot::Component {
    protected:
        void OnCollisionEnter(const Spot::Collision&) override { throw std::runtime_error("broken collision"); }
    };

    Scene scene;
    CreateGround(scene);
    Entity& ball = CreateBall(scene, {0.0f, 1.0f, 0.0f});
    auto& throwing = ball.AddComponent<Throwing>();
    auto& recorder = ball.AddComponent<EventRecorder>();
    Simulate(scene, 1.0f);
    CHECK(throwing.IsFaulted());
    CHECK(recorder.Has("enter Ground"));
}
