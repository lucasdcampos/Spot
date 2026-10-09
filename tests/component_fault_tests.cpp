#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Spot::Entity;
using Spot::Scene;

namespace {

using Calls = std::vector<std::string>;

// Records its hooks, and throws from the one it is told to.
class Faulty final : public Spot::Component {
public:
    Faulty(Calls& calls, std::string throwIn) : m_calls(calls), m_throwIn(std::move(throwIn)) {}

    int updates = 0;

protected:
    void OnStart() override { Run("start"); }
    void OnEnable() override { Run("enable"); }
    void OnDisable() override { Run("disable"); }
    void OnFixedUpdate(float /*fixedDeltaTime*/) override { Run("fixed"); }
    void OnUpdate(float /*deltaTime*/) override {
        ++updates;
        Run("update");
    }
    void OnLateUpdate(float /*deltaTime*/) override { Run("late"); }
    void OnDestroy() override { Run("destroy"); }
    void OnValidate() override { Run("validate"); }

private:
    void Run(const std::string& hook) {
        m_calls.push_back(hook);
        if (hook == m_throwIn) {
            throw std::runtime_error("broken " + hook);
        }
    }

    Calls& m_calls;
    std::string m_throwIn;
};

class Counter final : public Spot::Component {
public:
    int updates = 0;

protected:
    void OnUpdate(float /*deltaTime*/) override { ++updates; }
};

// Throws while broken, which is not saved.
class Fragile final : public Spot::Component {
public:
    bool broken = false;

protected:
    void OnUpdate(float /*deltaTime*/) override {
        if (broken) {
            throw std::runtime_error("broken");
        }
    }
};

class ThrowsInt final : public Spot::Component {
protected:
    void OnUpdate(float /*deltaTime*/) override { throw 42; }
};

std::vector<Spot::Tests::CapturedRecord> GetErrors(const Spot::Tests::CaptureSink& sink) {
    std::vector<Spot::Tests::CapturedRecord> errors;
    for (const auto& record : sink.GetRecords()) {
        if (record.level == Spot::LogLevel::Error) {
            errors.push_back(record);
        }
    }
    return errors;
}

} // namespace

TEST_CASE("A component that throws stops, and the others keep running", "[scene][fault]") {
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({sink});

    Scene scene;
    Calls calls;
    Entity& entity = scene.CreateEntity("Broken");
    auto& faulty = entity.AddComponent<Faulty>(calls, "update");
    auto& before = scene.CreateEntity("Before").AddComponent<Counter>();
    auto& after = entity.AddComponent<Counter>();

    scene.Update(0.016f);
    CHECK(faulty.IsFaulted());
    CHECK_FALSE(faulty.IsActiveAndEnabled());
    // Still enabled: the fault is its own state, which tools show.
    CHECK(faulty.IsEnabled());
    CHECK(calls == Calls{"start", "enable", "update", "disable"});
    CHECK(before.updates == 1);
    CHECK(after.updates == 1);

    scene.Update(0.016f);
    scene.Update(0.016f);
    CHECK(faulty.updates == 1);
    CHECK(before.updates == 3);
    CHECK(after.updates == 3);

    // Logged once, with the entity, the component's type and the hook.
    const auto errors = GetErrors(*sink);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].message.find("'Broken'") != std::string::npos);
    CHECK(errors[0].message.find("Faulty") != std::string::npos);
    CHECK(errors[0].message.find("OnUpdate()") != std::string::npos);
    CHECK(errors[0].message.find("broken update") != std::string::npos);

    // Enabling it again does not bring it back.
    faulty.SetEnabled(false);
    faulty.SetEnabled(true);
    scene.Update(0.016f);
    CHECK(faulty.updates == 1);

    // It still releases what it holds.
    calls.clear();
    entity.Destroy();
    CHECK(calls == Calls{"destroy"});
}

TEST_CASE("A component that throws in OnStart() never becomes active", "[scene][fault]") {
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({sink});

    Scene scene;
    Calls calls;
    Entity& entity = scene.CreateEntity();
    auto& faulty = entity.AddComponent<Faulty>(calls, "start");
    scene.Update(0.016f);
    scene.Update(0.016f);
    CHECK(faulty.IsFaulted());
    CHECK(calls == Calls{"start"});

    entity.Destroy();
    CHECK(calls == Calls{"start", "destroy"});
    CHECK(GetErrors(*sink).size() == 1);
}

TEST_CASE("Failures in every hook are contained", "[scene][fault]") {
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({sink});

    for (const char* hook : {"enable", "fixed", "late", "disable", "destroy", "validate"}) {
        CAPTURE(hook);
        const std::size_t errorsBefore = GetErrors(*sink).size();
        Scene scene;
        Calls calls;
        Entity& entity = scene.CreateEntity();
        auto& faulty = entity.AddComponent<Faulty>(calls, hook);
        auto& counter = entity.AddComponent<Counter>();
        Spot::ValidateComponent(faulty);
        // Enough time for a physics step, so OnFixedUpdate() runs.
        scene.Update(0.1f);
        faulty.SetEnabled(false);
        faulty.SetEnabled(true);
        scene.Update(0.1f);
        CHECK(counter.updates == 2);
        // Freed at once, so it is not looked at afterwards.
        entity.RemoveComponent(faulty);
        CHECK(GetErrors(*sink).size() > errorsBefore);
    }
}

TEST_CASE("Exceptions of any type fault the component", "[scene][fault]") {
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({sink});

    Scene scene;
    auto& component = scene.CreateEntity().AddComponent<ThrowsInt>();
    scene.Update(0.016f);
    CHECK(component.IsFaulted());
    const auto errors = GetErrors(*sink);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].message.find("not a std::exception") != std::string::npos);
}

TEST_CASE("Faults last until the scene is reloaded", "[scene][fault]") {
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({sink});
    Spot::RegisterComponent<Fragile>("FaultTestFragile");

    Scene scene;
    auto& fragile = scene.CreateEntity().AddComponent<Fragile>();
    fragile.broken = true;
    scene.Update(0.016f);
    REQUIRE(fragile.IsFaulted());

    Scene reloaded;
    REQUIRE(reloaded.LoadFromString(scene.SaveToString()));
    auto* loaded = reloaded.FindComponent<Fragile>();
    REQUIRE(loaded);
    reloaded.Update(0.016f);
    CHECK_FALSE(loaded->IsFaulted());
    CHECK(loaded->IsActiveAndEnabled());
    Spot::UnregisterComponent("FaultTestFragile");
}
