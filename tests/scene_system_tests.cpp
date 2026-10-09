#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Spot::Scene;
using Spot::SceneSystem;

namespace {

using Calls = std::vector<std::string>;

// Records its hooks as "<label>.<hook>".
class Recorder : public SceneSystem {
public:
    Recorder(Calls& calls, std::string label, int order = Spot::SystemOrder::Default)
        : SceneSystem(order), m_calls(calls), m_label(std::move(label)) {}

protected:
    void OnBeforeFixedSteps(float /*deltaTime*/) override { Record("before"); }
    void OnFixedUpdate(float /*fixedDeltaTime*/) override { Record("fixed"); }
    void OnAfterFixedSteps(float /*deltaTime*/) override { Record("after"); }
    void OnUpdate(float /*deltaTime*/) override { Record("update"); }
    void OnLateUpdate(float /*deltaTime*/) override { Record("late"); }
    void OnBeforeRender() override { Record("render"); }

    void Record(const char* hook) { m_calls.push_back(m_label + "." + hook); }

private:
    Calls& m_calls;
    std::string m_label;
};

// Distinct types, as a scene has one system of each.
class First final : public Recorder {
public:
    using Recorder::Recorder;
};

class Second final : public Recorder {
public:
    using Recorder::Recorder;
};

class Early final : public Recorder {
public:
    using Recorder::Recorder;
};

class ComponentRecorder final : public Spot::Component {
public:
    explicit ComponentRecorder(Calls& calls) : m_calls(calls) {}

protected:
    void OnFixedUpdate(float /*fixedDeltaTime*/) override { m_calls.emplace_back("component.fixed"); }
    void OnUpdate(float /*deltaTime*/) override { m_calls.emplace_back("component.update"); }
    void OnLateUpdate(float /*deltaTime*/) override { m_calls.emplace_back("component.late"); }

private:
    Calls& m_calls;
};

class Broken final : public SceneSystem {
public:
    int updates = 0;

protected:
    void OnUpdate(float /*deltaTime*/) override {
        ++updates;
        throw std::runtime_error("broken system");
    }
};

class Counter final : public SceneSystem {
public:
    int updates = 0;

protected:
    void OnUpdate(float /*deltaTime*/) override { ++updates; }
};

// Adds a Counter from its first update.
class Adder final : public SceneSystem {
protected:
    void OnUpdate(float /*deltaTime*/) override {
        if (!GetScene().GetSystem<Counter>()) {
            GetScene().AddSystem<Counter>();
        }
    }
};

} // namespace

TEST_CASE("Systems run at their points of the update, by order, after components", "[scene][systems]") {
    Scene scene;
    Calls calls;
    scene.CreateEntity().AddComponent<ComponentRecorder>(calls);
    scene.AddSystem<Second>(calls, "second");
    auto& first = scene.AddSystem<First>(calls, "first");
    scene.AddSystem<Early>(calls, "early", -10);
    CHECK(&first.GetScene() == &scene);
    CHECK(scene.GetSystem<First>() == &first);
    CHECK(scene.GetSystem<Recorder>() != nullptr);

    // One fixed step.
    scene.Update(Scene::DefaultFixedTimeStep * 1.5f);
    CHECK(calls == Calls{"early.before", "second.before", "first.before", "component.fixed", "early.fixed",
                         "second.fixed", "first.fixed", "early.after", "second.after", "first.after",
                         "component.update", "early.update", "second.update", "first.update", "component.late",
                         "early.late", "second.late", "first.late"});

    calls.clear();
    scene.PrepareToRender();
    CHECK(calls == Calls{"early.render", "second.render", "first.render"});
}

TEST_CASE("Physics is created when the scene first needs it", "[scene][systems]") {
    Scene scene;
    scene.CreateEntity("Empty");
    scene.Update(0.1f);
    CHECK(scene.GetSystem<Spot::PhysicsWorld>() == nullptr);

    scene.CreateEntity("Floor").AddComponent<Spot::BoxCollider>();
    scene.Update(0.1f);
    Spot::PhysicsWorld* physics = scene.GetSystem<Spot::PhysicsWorld>();
    REQUIRE(physics != nullptr);
    CHECK(&scene.GetPhysics() == physics);
    CHECK(physics->GetOrder() == Spot::SystemOrder::Physics);
    CHECK(physics->GetBodyCount() == 1);
}

TEST_CASE("A system that throws is skipped, and the others keep running", "[scene][systems]") {
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({sink});

    Scene scene;
    auto& broken = scene.AddSystem<Broken>();
    auto& counter = scene.AddSystem<Counter>();
    for (int i = 0; i < 3; ++i) {
        scene.Update(0.016f);
    }
    CHECK(broken.IsFaulted());
    CHECK(broken.updates == 1);
    CHECK(counter.updates == 3);

    std::size_t errors = 0;
    for (const auto& record : sink->GetRecords()) {
        if (record.level == Spot::LogLevel::Error) {
            ++errors;
            CHECK(record.message.find("Broken") != std::string::npos);
            CHECK(record.message.find("OnUpdate()") != std::string::npos);
            CHECK(record.message.find("broken system") != std::string::npos);
        }
    }
    CHECK(errors == 1);
}

TEST_CASE("Systems added while systems run join from the next hook", "[scene][systems]") {
    Scene scene;
    scene.AddSystem<Adder>();
    scene.Update(0.016f);
    auto* counter = scene.GetSystem<Counter>();
    REQUIRE(counter != nullptr);
    CHECK(counter->updates == 0);
    scene.Update(0.016f);
    CHECK(counter->updates == 1);
}
