#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <stdexcept>
#include <vector>

using Catch::Approx;
using namespace std::chrono_literals;

namespace {

class StepCounter final : public Spot::Component {
public:
    int count = 0;

protected:
    void OnFixedUpdate(float) override { ++count; }
};

class TimerOwner final : public Spot::Component {};

} // namespace

TEST_CASE("Scaled time pauses and resumes while unscaled time advances", "[gameplay][time]") {
    Spot::FrameClock clock;
    const Spot::FrameClock::TimePoint start{};
    clock.Start(start);
    REQUIRE(clock.SetTimeScale(0.5f));
    clock.Tick(start + 100ms);
    CHECK(clock.GetDeltaTime() == Approx(0.05));
    CHECK(clock.GetUnscaledDeltaTime() == Approx(0.1));
    REQUIRE(clock.SetTimeScale(0.0f));
    clock.Tick(start + 1s);
    CHECK(clock.GetDeltaTime() == 0.0f);
    CHECK(clock.GetTime() == Approx(0.05));
    CHECK(clock.GetUnscaledTime() == Approx(0.35));
    REQUIRE(clock.SetTimeScale(2.0f));
    clock.Tick(start + 1100ms);
    CHECK(clock.GetTime() == Approx(0.25));
    CHECK_FALSE(clock.SetTimeScale(-1.0f));
    CHECK_FALSE(clock.SetTimeScale(std::numeric_limits<float>::infinity()));
    CHECK_FALSE(clock.SetTimeScale(std::numeric_limits<float>::quiet_NaN()));
    CHECK(clock.GetTimeScale() == 2.0f);
    clock.Tick(start);
    CHECK(clock.GetDeltaTime() == 0.0f);
}

TEST_CASE("Global time scale drives the application's clock and fixed physics", "[gameplay][time]") {
    struct RestoreTime {
        float scale = Spot::Time::GetTimeScale();
        ~RestoreTime() {
            Spot::Time::SetClock(nullptr);
            Spot::Time::SetTimeScale(scale);
        }
    } restore;
    Spot::FrameClock clock;
    clock.Start({});
    Spot::Time::SetClock(&clock);
    REQUIRE(Spot::Time::SetTimeScale(0.5f));
    Spot::Scene scene;
    scene.SetFixedTimeStep(0.01f);
    scene.GetPhysics().SetGravity({});
    auto& entity = scene.CreateEntity();
    entity.AddComponent<Spot::BoxCollider>();
    auto& body = entity.AddComponent<Spot::Rigidbody>();
    scene.Update(0.0f);
    body.SetLinearVelocity({10.0f, 0.0f, 0.0f});
    clock.Tick(Spot::FrameClock::TimePoint{} + 100ms);
    scene.Update(Spot::Time::GetDeltaTime());
    CHECK(entity.GetTransform().GetPosition().x == Approx(0.5).margin(0.01));
    REQUIRE(Spot::Time::SetTimeScale(0.0f));
    clock.Tick(Spot::FrameClock::TimePoint{} + 200ms);
    scene.Update(Spot::Time::GetDeltaTime());
    CHECK(entity.GetTransform().GetPosition().x == Approx(0.5).margin(0.01));
    CHECK(Spot::Time::GetUnscaledDeltaTime() == Approx(0.1));
}

TEST_CASE("Fixed steps retain fractional time after stalls and handle extreme deltas", "[gameplay][time]") {
    Spot::Scene scene;
    scene.SetFixedTimeStep(0.25f);
    auto& counter = scene.CreateEntity().AddComponent<StepCounter>();
    scene.Update(10.125f);
    CHECK(counter.count == Spot::Scene::MaxFixedStepsPerUpdate);
    scene.Update(0.125f);
    CHECK(counter.count == Spot::Scene::MaxFixedStepsPerUpdate + 1);
    scene.Update(std::numeric_limits<float>::max());
    CHECK(counter.count == 2 * Spot::Scene::MaxFixedStepsPerUpdate + 1);
    scene.Update(std::numeric_limits<float>::infinity());
    scene.Update(-1.0f);
    CHECK(counter.count == 2 * Spot::Scene::MaxFixedStepsPerUpdate + 1);
}

TEST_CASE("Timers pause, preserve repeat phase and coalesce missed repetitions", "[gameplay][timers]") {
    Spot::TimerQueue timers;
    int once = 0;
    int repeated = 0;
    const auto single = timers.Invoke(0.5f, [&] { ++once; });
    const auto repeat = timers.InvokeRepeating(0.25f, 0.25f, [&] { ++repeated; });
    timers.Update(0.0f);
    timers.Update(0.125f);
    CHECK(once == 0);
    CHECK(repeated == 0);
    CHECK(timers.GetTimeLeft(single) == Approx(0.375f));
    timers.Update(0.625f);
    CHECK(once == 1);
    CHECK_FALSE(timers.IsPending(single));
    CHECK(repeated == 1);
    CHECK(timers.GetTimeLeft(repeat) == Approx(0.25f));
    timers.Update(0.25f);
    CHECK(repeated == 2);
    CHECK(timers.Cancel(repeat));
    CHECK_FALSE(timers.Cancel(repeat));
    timers.Update(1.0f);
    CHECK(repeated == 2);
    CHECK(timers.Invoke(-1.0f, [] {}) == Spot::InvokeId::Invalid);
    CHECK(timers.Invoke(std::numeric_limits<float>::quiet_NaN(), [] {}) == Spot::InvokeId::Invalid);
    CHECK(timers.Invoke(0.0f, {}) == Spot::InvokeId::Invalid);
    CHECK(timers.InvokeRepeating(0.0f, 0.0f, [] {}) == Spot::InvokeId::Invalid);
}

TEST_CASE("Timer callbacks can cancel, schedule and update without invalidation", "[gameplay][timers]") {
    Spot::TimerQueue timers;
    std::vector<int> calls;
    Spot::InvokeId cancelled = Spot::InvokeId::Invalid;
    timers.Invoke(0.0f, [&] {
        calls.push_back(1);
        timers.Cancel(cancelled);
        timers.Invoke(0.0f, [&] { calls.push_back(3); });
        timers.Update(1.0f);
    });
    cancelled = timers.Invoke(0.0f, [&] { calls.push_back(2); });
    timers.Update(0.25f);
    CHECK(calls == std::vector<int>{1});
    timers.Update(0.25f);
    CHECK(calls == std::vector<int>{1, 3});
    const auto repeat = timers.InvokeRepeating(0.0f, 0.1f, [&] { timers.CancelAll(); });
    timers.Update(0.25f);
    CHECK_FALSE(timers.IsPending(repeat));
}

TEST_CASE("Component timers pause with activity and release their callbacks on removal", "[gameplay][timers]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& owner = entity.AddComponent<TimerOwner>();
    int calls = 0;
    const auto id = owner.Invoke(0.5f, [&] { ++calls; });
    scene.Update(0.25f);
    owner.SetEnabled(false);
    scene.Update(1.0f);
    CHECK(calls == 0);
    CHECK(owner.GetInvokeTimeLeft(id) == Approx(0.25f));
    owner.SetEnabled(true);
    scene.Update(0.0f);
    CHECK(calls == 0);
    scene.Update(0.25f);
    CHECK(calls == 1);
    auto captured = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = captured;
    owner.Invoke(10.0f, [captured] {});
    captured.reset();
    CHECK_FALSE(weak.expired());
    entity.RemoveComponent(owner);
    CHECK(weak.expired());
    scene.Update(20.0f);
    CHECK(calls == 1);
}

TEST_CASE("Destroying or faulting a timer owner stops its remaining callbacks", "[gameplay][timers]") {
    Spot::Scene scene;
    int calls = 0;
    auto& entity = scene.CreateEntity();
    const auto id = entity.GetId();
    auto& owner = entity.AddComponent<TimerOwner>();
    owner.Invoke(0.0f, [&] { entity.Destroy(); });
    owner.Invoke(0.0f, [&] { ++calls; });
    auto& broken = scene.CreateEntity().AddComponent<TimerOwner>();
    broken.Invoke(0.0f, [] { throw std::runtime_error("timer failure"); });
    const auto skipped = broken.Invoke(0.0f, [&] { ++calls; });
    auto& healthy = scene.CreateEntity().AddComponent<TimerOwner>();
    healthy.Invoke(0.0f, [&] { ++calls; });
    scene.Update(0.1f);
    CHECK(scene.GetEntity(id) == nullptr);
    CHECK(broken.IsFaulted());
    CHECK_FALSE(broken.IsInvokePending(skipped));
    CHECK(calls == 1);
}
