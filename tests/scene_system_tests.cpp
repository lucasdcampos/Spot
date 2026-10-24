#include "log_capture.h"
#include <spot/scene/physics/physics_world.h>

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
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
public:
    bool pendingVisible = false;
    bool pendingReused = false;

protected:
    void OnUpdate(float /*deltaTime*/) override {
        if (!GetScene().GetSystem<Counter>()) {
            auto& added = GetScene().GetOrAddSystem<Counter>();
            pendingVisible = GetScene().GetSystem<Counter>() == &added;
            pendingReused = &GetScene().GetOrAddSystem<Counter>() == &added;
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
    CHECK(scene.GetSystem<Recorder>() == nullptr);

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

TEST_CASE("Systems are reused by exact type and forward construction arguments", "[scene][systems]") {
    class Configured final : public SceneSystem {
    public:
        explicit Configured(std::unique_ptr<int> value) : value(std::move(value)) {}
        std::unique_ptr<int> value;
    };
    Scene scene;
    auto value = std::make_unique<int>(42);
    auto& first = scene.GetOrAddSystem<Configured>(std::move(value));
    CHECK(value == nullptr);
    REQUIRE(first.value != nullptr);
    CHECK(*first.value == 42);
    auto unused = std::make_unique<int>(99);
    CHECK(&scene.GetOrAddSystem<Configured>(std::move(unused)) == &first);
    REQUIRE(unused != nullptr);
    CHECK(*unused == 99);
    CHECK(scene.GetSystem<Configured>() == &first);
    CHECK(scene.GetSystem<SceneSystem>() == nullptr);

    Calls calls;
    auto& derived = scene.GetOrAddSystem<First>(calls, "derived");
    CHECK(scene.GetSystem<Recorder>() == nullptr);
    auto& base = scene.GetOrAddSystem<Recorder>(calls, "base");
    CHECK(scene.GetSystem<Recorder>() == &base);
    CHECK(scene.GetSystem<First>() == &derived);
    CHECK(static_cast<Recorder*>(&derived) != &base);
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
    CHECK(&scene.GetOrAddSystem<Spot::PhysicsWorld>() == physics);
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
    auto& adder = scene.AddSystem<Adder>();
    scene.Update(0.016f);
    CHECK(adder.pendingVisible);
    CHECK(adder.pendingReused);
    auto* counter = scene.GetSystem<Counter>();
    REQUIRE(counter != nullptr);
    CHECK(counter->updates == 0);
    scene.Update(0.016f);
    CHECK(counter->updates == 1);
}

namespace {

class NotificationRecorder final : public SceneSystem {
public:
    explicit NotificationRecorder(Calls& calls) : m_calls(calls) {}

protected:
    void OnComponentAdded(Spot::Component&) override { m_calls.emplace_back("added"); }
    void OnComponentRemoved(Spot::Component&) override { m_calls.emplace_back("removed"); }
    void OnEntityLayerChanged(Spot::Entity&) override { m_calls.emplace_back("layer"); }
    void OnEntityDestroyed(Spot::Entity&) override { m_calls.emplace_back("destroyed"); }
    void OnEntityMoved(Spot::Entity& entity, Scene& source, Scene& destination) override {
        REQUIRE(&entity.GetScene() == &GetScene());
        m_calls.emplace_back(&GetScene() == &source ? "leaving" : "entered");
        REQUIRE(&source != &destination);
    }

private:
    Calls& m_calls;
};

} // namespace

TEST_CASE("Scene systems hear structural changes immediately", "[scene][systems]") {
    Calls sourceCalls;
    Calls destinationCalls;
    Scene source;
    Scene destination;
    source.AddSystem<NotificationRecorder>(sourceCalls);
    destination.AddSystem<NotificationRecorder>(destinationCalls);
    auto& entity = source.CreateEntity();
    auto& component = entity.AddComponent<ComponentRecorder>(sourceCalls);
    REQUIRE(sourceCalls == Calls{"added"});
    entity.MoveComponent(component, 0);
    REQUIRE(sourceCalls == Calls{"added"});
    entity.SetLayer(2);
    entity.SetLayer(2);
    entity.RemoveComponent(component);
    REQUIRE(sourceCalls == Calls{"added", "layer", "removed"});
    entity.MoveToScene(destination);
    REQUIRE(sourceCalls == Calls{"added", "layer", "removed", "leaving"});
    REQUIRE(destinationCalls == Calls{"entered"});
    entity.Destroy();
    REQUIRE(destinationCalls == Calls{"entered", "destroyed"});
}

namespace {

class BrokenNotification final : public SceneSystem {
protected:
    void OnComponentAdded(Spot::Component&) override { throw std::runtime_error("broken notification"); }
};

class NotificationAdder final : public SceneSystem {
public:
    explicit NotificationAdder(Calls& calls) : m_calls(calls) {}

protected:
    void OnComponentAdded(Spot::Component&) override { GetScene().GetOrAddSystem<NotificationRecorder>(m_calls); }

private:
    Calls& m_calls;
};

} // namespace

TEST_CASE("Scene notifications fault only the throwing system", "[scene][systems]") {
    Calls calls;
    Scene scene;
    auto& broken = scene.AddSystem<BrokenNotification>();
    scene.AddSystem<NotificationRecorder>(calls);
    auto& entity = scene.CreateEntity();
    entity.AddComponent<ComponentRecorder>(calls);
    CHECK(broken.IsFaulted());
    entity.SetLayer(3);
    CHECK(calls == Calls{"added", "layer"});
}

TEST_CASE("Systems added by a notification hear subsequent changes", "[scene][systems]") {
    Calls calls;
    Scene scene;
    scene.AddSystem<NotificationAdder>(calls);
    auto& entity = scene.CreateEntity();
    entity.AddComponent<ComponentRecorder>(calls);
    CHECK(calls.empty());
    entity.SetLayer(3);
    CHECK(calls == Calls{"layer"});
    scene.Update(0.0f);
    CHECK(scene.GetSystem<NotificationRecorder>() != nullptr);
}

TEST_CASE("Destroyed children are notified once when their parent is destroyed", "[scene][systems]") {
    Calls calls;
    Scene scene;
    scene.AddSystem<NotificationRecorder>(calls);
    auto& parent = scene.CreateEntity();
    auto& child = parent.CreateChild();
    scene.ForEachEntity([&](Spot::Entity& entity) {
        if (&entity == &parent) {
            child.Destroy();
            parent.Destroy();
        }
    });
    CHECK(calls == Calls{"destroyed", "destroyed"});
}

namespace {

class MoveMutation final : public SceneSystem {
public:
    std::function<void(Spot::Entity&, Scene&, Scene&)> callback;

protected:
    void OnEntityMoved(Spot::Entity& entity, Scene& source, Scene& destination) override {
        if (callback) {
            callback(entity, source, destination);
        }
    }
};

} // namespace

TEST_CASE("Scene transfers tolerate changes from move notifications", "[scene][systems]") {
    Scene source;
    Scene destination;
    Scene third;
    auto& mutations = source.AddSystem<MoveMutation>();
    auto& parent = source.CreateEntity();
    auto& child = parent.CreateChild();
    const auto parentId = parent.GetId();
    const auto childId = child.GetId();

    SECTION("Nested transfers of the moving entity are ignored") {
        mutations.callback = [&](Spot::Entity& entity, Scene&, Scene&) { entity.MoveToScene(third); };
        parent.MoveToScene(destination);
        CHECK(destination.GetEntity(parentId) == &parent);
        CHECK(destination.GetEntity(childId) == &child);
        CHECK(third.GetEntityCount() == 0);
    }
    SECTION("Destroying the entity cancels its transfer") {
        mutations.callback = [](Spot::Entity& entity, Scene&, Scene&) { entity.Destroy(); };
        parent.MoveToScene(destination);
        CHECK(source.GetEntityCount() == 0);
        CHECK(destination.GetEntityCount() == 0);
    }
    SECTION("Reparented children stay in their chosen scene") {
        mutations.callback = [&](Spot::Entity& entity, Scene&, Scene&) {
            if (entity.GetId() == parentId) {
                child.SetParent(nullptr);
                child.MoveToScene(third);
            }
        };
        parent.MoveToScene(destination);
        CHECK(destination.GetEntity(parentId) == &parent);
        CHECK(third.GetEntity(childId) == &child);
        CHECK(parent.GetChildren().empty());
    }
    SECTION("Destination callbacks can destroy the transferred hierarchy") {
        destination.AddSystem<MoveMutation>().callback = [&](Spot::Entity& entity, Scene&, Scene&) {
            if (entity.GetId() == parentId) {
                entity.Destroy();
            }
        };
        parent.MoveToScene(destination);
        CHECK(source.GetEntityCount() == 0);
        CHECK(destination.GetEntityCount() == 0);
    }
}

TEST_CASE("Canceled scene transfers leave no stale audio source registrations", "[scene][systems][audio]") {
    Scene source;
    Scene destination;
    auto& entity = source.CreateEntity();
    const auto id = entity.GetId();
    entity.AddComponent<Spot::AudioSource>();
    source.Update(0.0f);
    source.AddSystem<MoveMutation>().callback = [](Spot::Entity& moving, Scene&, Scene&) { moving.Destroy(); };
    entity.MoveToScene(destination);
    CHECK(source.GetEntity(id) == nullptr);
    CHECK(destination.GetEntity(id) == nullptr);
    source.Update(0.0f);
    destination.Update(0.0f);
}
