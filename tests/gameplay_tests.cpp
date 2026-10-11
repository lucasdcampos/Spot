#include <spot.h>

#include "temp_directory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
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

class ReferenceOwner final : public Spot::Component {
public:
    Spot::ComponentRef<Spot::PointLight> target;

protected:
    void Serialize(Spot::Archive& archive) override { archive.Field("target", target); }
};

Spot::Task ChildTask(std::vector<int>& calls) {
    calls.push_back(4);
    co_await Spot::WaitForSeconds{0.25f};
    calls.push_back(5);
}

Spot::Task AwaitAll(std::vector<int>& calls, bool& ready) {
    calls.push_back(0);
    co_await Spot::NextFrame{};
    calls.push_back(1);
    co_await Spot::WaitForSeconds{0.5f};
    calls.push_back(2);
    co_await Spot::WaitUntil{[&] { return ready; }};
    calls.push_back(3);
    co_await ChildTask(calls);
    calls.push_back(6);
}

struct TaskLifetime {
    int& destroyed;
    ~TaskLifetime() { ++destroyed; }
};

Spot::Task WaitingChild(int& destroyed) {
    TaskLifetime lifetime{destroyed};
    co_await Spot::WaitForSeconds{10.0f};
}

Spot::Task WaitingParent(int& destroyed) {
    TaskLifetime lifetime{destroyed};
    co_await WaitingChild(destroyed);
}

Spot::Task ThrowingCondition() {
    co_await Spot::WaitUntil{[]() -> bool { throw std::runtime_error("condition failure"); }};
}

Spot::Task CatchChildError(int& caught) {
    try {
        co_await ThrowingCondition();
    } catch (const std::runtime_error&) {
        ++caught;
    }
}

Spot::Task CountFrames(int& calls) {
    ++calls;
    co_await Spot::NextFrame{};
    ++calls;
    co_await Spot::NextFrame{};
    ++calls;
}

class CoroutineStarter final : public Spot::Component {
public:
    int calls = 0;

protected:
    void OnStart() override { StartCoroutine(CountFrames(calls)); }
};

Spot::Task CancelSelf(Spot::TaskScheduler& scheduler, Spot::TaskId& id, int& calls) {
    co_await Spot::NextFrame{};
    scheduler.Cancel(id);
    ++calls;
    co_await Spot::NextFrame{};
    ++calls;
}

Spot::Task DestroyOwner(Spot::Entity& entity, int& calls) {
    co_await Spot::WaitUntil{[&] {
        entity.Destroy();
        return true;
    }};
    ++calls;
}

Spot::Task FailTask() {
    co_await Spot::NextFrame{};
    throw std::runtime_error("coroutine failure");
}

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

TEST_CASE("Tween easing has stable endpoints and interpolation supports colors and rotations", "[gameplay][tweens]") {
    for (const auto ease : {Spot::Ease::Linear, Spot::Ease::QuadIn, Spot::Ease::QuadOut, Spot::Ease::QuadInOut,
                            Spot::Ease::CubicIn, Spot::Ease::CubicOut, Spot::Ease::CubicInOut, Spot::Ease::SineInOut}) {
        CHECK(Spot::EvaluateEase(ease, -1.0f) == Approx(0.0f));
        CHECK(Spot::EvaluateEase(ease, 2.0f) == Approx(1.0f));
        float previous = 0.0f;
        for (int step = 1; step <= 10; ++step) {
            const float value = Spot::EvaluateEase(ease, static_cast<float>(step) / 10.0f);
            CHECK(value >= previous);
            previous = value;
        }
    }
    CHECK(Spot::EvaluateEase(Spot::Ease::QuadIn, 0.5f) == Approx(0.25f));
    CHECK(Spot::Interpolate(Spot::Color::Red, Spot::Color::Blue, 0.5f) == Spot::Color{0.5f, 0.0f, 0.5f, 1.0f});
    const auto end = Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, Spot::Pi * 0.5f);
    const auto half = Spot::Interpolate(Spot::Quaternion::Identity(), end, 0.5f);
    CHECK((half * Spot::Vector3{1.0f, 0.0f, 0.0f}).x == Approx(std::sqrt(0.5f)));
}

TEST_CASE("Tween sequences consume overshoot and complete once", "[gameplay][tweens]") {
    Spot::TweenPlayer player;
    float value = 0.0f;
    int completions = 0;
    const auto set = [&](float sample) { value = sample; };
    const auto sequence = Spot::Tween::Sequence({Spot::Tween::Value(0.5f, 0.0f, 1.0f, set), Spot::Tween::Delay(0.25f),
                                                 Spot::Tween::Value(0.5f, 1.0f, 0.0f, set, Spot::Ease::QuadIn)});
    CHECK(sequence.GetDuration() == Approx(1.25));
    Spot::TweenOptions options;
    options.onComplete = [&] { ++completions; };
    const auto id = player.Play(sequence, std::move(options));
    player.Update(0.0f);
    CHECK(value == 0.0f);
    player.Update(1.0f);
    CHECK(value == Approx(0.75f));
    CHECK(player.IsPlaying(id));
    player.Update(0.5f);
    CHECK(value == 0.0f);
    CHECK(completions == 1);
    CHECK_FALSE(player.IsPlaying(id));
    player.Update(1.0f);
    CHECK(completions == 1);
    CHECK(player.Play(Spot::Tween(-1.0f)) == Spot::TweenId::Invalid);
    CHECK(player.Play(Spot::Tween::Sequence({Spot::Tween(std::numeric_limits<float>::infinity())})) ==
          Spot::TweenId::Invalid);
}

TEST_CASE("Tween callbacks safely cancel and schedule while failures stay isolated", "[gameplay][tweens]") {
    Spot::TweenPlayer player;
    int calls = 0;
    int errors = 0;
    Spot::TweenId id = Spot::TweenId::Invalid;
    id = player.Play(Spot::Tween(1.0f, [&](float) {
        ++calls;
        player.Cancel(id);
        player.Play(Spot::Tween(0.0f, [&](float) { ++calls; }));
        player.Update(1.0f);
    }));
    Spot::TweenOptions options;
    options.onError = [&](std::exception_ptr) { ++errors; };
    player.Play(Spot::Tween(0.0f, [](float) { throw std::runtime_error("tween failure"); }), std::move(options));
    player.Update(0.25f);
    CHECK(calls == 1);
    CHECK(errors == 1);
    CHECK_FALSE(player.IsPlaying(id));
    player.Update(0.25f);
    CHECK(calls == 2);
}

TEST_CASE("Scene tweens follow activity and resolve transform targets by identity", "[gameplay][tweens]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& owner = entity.AddComponent<TimerOwner>();
    auto& tweens = Spot::TweenSystem::Get(scene);
    const auto id = tweens.Play(owner, Spot::TweenSystem::MoveTo(entity, {2.0f, 0.0f, 0.0f}, 1.0f));
    scene.Update(0.25f);
    CHECK(entity.GetTransform().GetPosition().x == Approx(0.5f));
    owner.SetEnabled(false);
    scene.Update(0.5f);
    CHECK(entity.GetTransform().GetPosition().x == Approx(0.5f));
    owner.SetEnabled(true);
    scene.Update(0.25f);
    CHECK(entity.GetTransform().GetPosition().x == Approx(1.0f));
    entity.RemoveComponent(owner);
    scene.Update(0.25f);
    CHECK_FALSE(tweens.IsPlaying(id));
    CHECK(entity.GetTransform().GetPosition().x == Approx(1.0f));
    const auto target = entity.GetId();
    const auto unowned = tweens.Play(Spot::TweenSystem::ScaleTo(entity, {2.0f, 2.0f, 2.0f}, 0.25f));
    entity.Destroy();
    scene.Update(0.25f);
    CHECK(scene.GetEntity(target) == nullptr);
    CHECK_FALSE(tweens.IsPlaying(unowned));

    auto& rotating = scene.CreateEntity();
    auto& rotatingOwner = rotating.AddComponent<TimerOwner>();
    const auto rotation = Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, Spot::Pi * 0.5f);
    tweens.Play(rotatingOwner, Spot::TweenSystem::RotateTo(rotating, rotation, 0.5f));
    tweens.Play(rotatingOwner, Spot::TweenSystem::ScaleTo(rotating, {2.0f, 2.0f, 2.0f}, 0.5f));
    scene.Update(0.25f);
    CHECK(rotating.GetTransform().GetScale().x == Approx(1.5f));
    CHECK((rotating.GetTransform().GetRotation() * Spot::Vector3{1.0f, 0.0f, 0.0f}).x == Approx(std::sqrt(0.5f)));
    int afterDestruction = 0;
    const auto selfDestruct =
        tweens.Play(rotatingOwner, Spot::Tween::Sequence({Spot::Tween(0.0f, [&](float) { rotating.Destroy(); }),
                                                          Spot::Tween(0.0f, [&](float) { ++afterDestruction; })}));
    scene.Update(0.25f);
    CHECK_FALSE(tweens.IsPlaying(selfDestruct));
    CHECK(afterDestruction == 0);
}

TEST_CASE("Coroutines await frames, scaled seconds, conditions and nested tasks", "[gameplay][coroutines]") {
    Spot::TaskScheduler scheduler;
    std::vector<int> calls;
    bool ready = false;
    const auto id = scheduler.Start(AwaitAll(calls, ready));
    CHECK(calls == std::vector<int>{0});
    scheduler.Update(0.0f);
    CHECK(calls == std::vector<int>{0});
    scheduler.Update(0.1f);
    CHECK(calls == std::vector<int>{0, 1});
    scheduler.Update(0.25f);
    CHECK(calls == std::vector<int>{0, 1});
    scheduler.Update(0.25f);
    CHECK(calls == std::vector<int>{0, 1, 2});
    scheduler.Update(0.1f);
    CHECK(calls.size() == 3);
    ready = true;
    scheduler.Update(0.1f);
    CHECK(calls == std::vector<int>{0, 1, 2, 3, 4});
    scheduler.Update(0.125f);
    CHECK(scheduler.IsRunning(id));
    scheduler.Update(0.125f);
    CHECK(calls == std::vector<int>{0, 1, 2, 3, 4, 5, 6});
    CHECK_FALSE(scheduler.IsRunning(id));
}

TEST_CASE("Cancelling a coroutine releases nested frames and supports cancellation during execution",
          "[gameplay][coroutines]") {
    Spot::TaskScheduler scheduler;
    int destroyed = 0;
    const auto parent = scheduler.Start(WaitingParent(destroyed));
    CHECK(destroyed == 0);
    CHECK(scheduler.Cancel(parent));
    CHECK(destroyed == 2);
    CHECK_FALSE(scheduler.Cancel(parent));
    int calls = 0;
    Spot::TaskId self = Spot::TaskId::Invalid;
    self = scheduler.Start(CancelSelf(scheduler, self, calls));
    scheduler.Update(0.1f);
    CHECK(calls == 1);
    CHECK_FALSE(scheduler.IsRunning(self));
    scheduler.Update(0.1f);
    CHECK(calls == 1);
    CHECK(scheduler.Start({}) == Spot::TaskId::Invalid);
}

TEST_CASE("Nested coroutine failures can be caught by their parent", "[gameplay][coroutines]") {
    Spot::TaskScheduler scheduler;
    int caught = 0;
    const auto id = scheduler.Start(CatchChildError(caught));
    scheduler.Update(0.1f);
    CHECK(caught == 1);
    CHECK_FALSE(scheduler.IsRunning(id));
    const auto broken = scheduler.Start(FailTask());
    CHECK_THROWS_AS(scheduler.Update(0.1f), std::runtime_error);
    CHECK_FALSE(scheduler.IsRunning(broken));
}

TEST_CASE("Component coroutines start synchronously and resume on the next scene update", "[gameplay][coroutines]") {
    Spot::Scene scene;
    auto& owner = scene.CreateEntity().AddComponent<CoroutineStarter>();
    scene.Update(0.1f);
    CHECK(owner.calls == 1);
    owner.SetEnabled(false);
    scene.Update(0.1f);
    CHECK(owner.calls == 1);
    owner.SetEnabled(true);
    scene.Update(0.0f);
    CHECK(owner.calls == 1);
    scene.Update(0.1f);
    CHECK(owner.calls == 2);
    scene.Update(0.1f);
    CHECK(owner.calls == 3);
    int destroyed = 0;
    owner.StartCoroutine(WaitingParent(destroyed));
    owner.GetEntity().RemoveComponent(owner);
    CHECK(destroyed == 2);
}

TEST_CASE("Owner removal during a coroutine condition prevents resumption and faults stay isolated",
          "[gameplay][coroutines]") {
    Spot::Scene scene;
    int calls = 0;
    auto& removedEntity = scene.CreateEntity();
    const auto entityId = removedEntity.GetId();
    auto& removed = removedEntity.AddComponent<TimerOwner>();
    removed.StartCoroutine(DestroyOwner(removedEntity, calls));
    auto& broken = scene.CreateEntity().AddComponent<TimerOwner>();
    broken.StartCoroutine(FailTask());
    auto& healthy = scene.CreateEntity().AddComponent<TimerOwner>();
    const auto running = healthy.StartCoroutine(CountFrames(calls));
    scene.Update(0.1f);
    CHECK(scene.GetEntity(entityId) == nullptr);
    CHECK(broken.IsFaulted());
    CHECK(calls == 2);
    CHECK(healthy.IsCoroutineRunning(running));
    scene.Update(0.1f);
    CHECK(calls == 3);
    CHECK_FALSE(healthy.IsCoroutineRunning(running));
}

TEST_CASE("Signal connections disconnect at scope exit and move without duplicate subscriptions",
          "[gameplay][signals]") {
    Spot::Signal<int> signal;
    int total = 0;
    {
        auto connection = signal.Connect([&](int value) { total += value; });
        CHECK(connection.IsConnected());
        signal.Emit(2);
        auto moved = std::move(connection);
        CHECK_FALSE(connection.IsConnected());
        CHECK(moved.IsConnected());
        signal.Emit(3);
        auto replaced = signal.Connect([&](int value) { total += value * 10; });
        replaced = std::move(moved);
        signal.Emit(1);
        CHECK(total == 6);
        replaced.Disconnect();
        CHECK_FALSE(replaced.IsConnected());
        replaced.Disconnect();
    }
    signal.Emit(5);
    CHECK(total == 6);
    CHECK_FALSE(signal.Connect({}).IsConnected());
}

TEST_CASE("Signals defer new listeners and skip listeners disconnected during dispatch", "[gameplay][signals]") {
    Spot::Signal<int> signal;
    std::vector<int> calls;
    Spot::SignalConnection second;
    Spot::SignalConnection third;
    auto first = signal.Connect([&](int value) {
        calls.push_back(value);
        second.Disconnect();
        if (!third.IsConnected()) {
            third = signal.Connect([&](int received) { calls.push_back(received * 3); });
        }
    });
    second = signal.Connect([&](int value) { calls.push_back(value * 2); });
    signal.Emit(1);
    CHECK(calls == std::vector<int>{1});
    signal.Emit(2);
    CHECK(calls == std::vector<int>{1, 2, 6});
    signal.DisconnectAll();
    CHECK_FALSE(first.IsConnected());
    CHECK_FALSE(third.IsConnected());
    signal.Emit(3);
    CHECK(calls.size() == 3);
}

TEST_CASE("Destroying a signal during dispatch stops remaining listeners and expires connections",
          "[gameplay][signals]") {
    auto signal = std::make_unique<Spot::Signal<>>();
    int calls = 0;
    auto destroy = signal->Connect([&] { signal.reset(); });
    auto later = signal->Connect([&] { ++calls; });
    signal->Emit();
    CHECK(calls == 0);
    CHECK_FALSE(destroy.IsConnected());
    CHECK_FALSE(later.IsConnected());
    destroy.Disconnect();
    later.Disconnect();
}

TEST_CASE("Signals preserve reference arguments and propagate callback failures", "[gameplay][signals]") {
    Spot::Signal<int&> signal;
    int value = 0;
    auto first = signal.Connect([](int& received) { ++received; });
    auto broken = signal.Connect([](int&) { throw std::runtime_error("signal failure"); });
    auto last = signal.Connect([](int& received) { received += 10; });
    CHECK_THROWS_AS(signal.Emit(value), std::runtime_error);
    CHECK(value == 1);
    broken.Disconnect();
    signal.Emit(value);
    CHECK(value == 12);
}

TEST_CASE("Typed component references resolve by scene and survive target removal", "[gameplay][references]") {
    Spot::Scene scene;
    auto& target = scene.CreateEntity();
    auto& light = target.AddComponent<Spot::PointLight>();
    Spot::ComponentRef<Spot::PointLight> reference(&light);
    CHECK(reference.Get(scene) == &light);
    CHECK(reference.IsValid(scene));
    Spot::Scene other;
    CHECK(reference.Get(other) == nullptr);
    target.RemoveComponent(light);
    CHECK_FALSE(reference.IsValid(scene));
    auto& replacement = target.AddComponent<Spot::PointLight>();
    CHECK(reference.Get(scene) == &replacement);
    target.Destroy();
    CHECK(reference.Get(scene) == nullptr);
    reference.Reset();
    CHECK(reference.GetEntityId() == Spot::EntityId::Invalid);
    auto& wrong = scene.CreateEntity();
    reference.SetEntityId(wrong.GetId());
    CHECK_FALSE(reference.IsValid(scene));
}

TEST_CASE("Typed references serialize as entity ids and remap when instantiating trees", "[gameplay][references]") {
    Spot::RegisterComponent<ReferenceOwner>("GameplayReferenceOwner");
    struct Registration {
        ~Registration() { Spot::UnregisterComponent("GameplayReferenceOwner"); }
    } registration;
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Tree");
    auto& target = scene.CreateEntity("Target");
    target.SetParent(&root);
    auto& light = target.AddComponent<Spot::PointLight>();
    auto& owner = root.AddComponent<ReferenceOwner>();
    owner.target.Set(&light);
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const auto* loadedOwner = loaded.FindComponent<ReferenceOwner>();
    REQUIRE(loadedOwner);
    REQUIRE(loadedOwner->target.Get(loaded));
    CHECK(loadedOwner->target.Get(loaded)->GetEntity().GetName() == "Target");
    Spot::Entity* roots[] = {&root};
    const auto copies = scene.Instantiate(scene.SaveEntitiesToString(roots));
    REQUIRE(copies.size() == 1);
    const auto* copyOwner = copies.front()->GetComponent<ReferenceOwner>();
    REQUIRE(copyOwner);
    REQUIRE(copyOwner->target.Get(scene));
    CHECK(copyOwner->target.GetEntityId() != target.GetId());
    CHECK(copyOwner->target.Get(scene)->GetEntity().GetParent() == copies.front());
}

TEST_CASE("Scene snapshots retain inline materials and body velocities", "[gameplay][save]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Painted prop");
    auto& renderer = entity.AddComponent<Spot::MeshRenderer>();
    Spot::Material material;
    material.baseColor = {0.2f, 0.3f, 0.4f, 0.5f};
    material.metallic = 0.6f;
    material.roughness = 0.2f;
    material.normalScale = 0.7f;
    material.occlusionStrength = 0.8f;
    material.emissive = {2.0f, 1.0f, 0.5f, 1.0f};
    material.alphaMode = Spot::AlphaMode::Transparent;
    material.alphaCutoff = 0.1f;
    material.doubleSided = true;
    material.worldTextureSize = 3.0f;
    const Spot::AssetRef<Spot::Texture> texture{Spot::BuiltInAssets::GridTexture, {}};
    material.baseColorTexture = texture;
    material.metallicRoughnessTexture = texture;
    material.normalTexture = texture;
    material.occlusionTexture = texture;
    material.emissiveTexture = texture;
    renderer.SetMaterial(material);
    entity.AddComponent<Spot::BoxCollider>();
    auto& body = entity.AddComponent<Spot::Rigidbody>();
    body.SetLinearVelocity({1.0f, 2.0f, 3.0f});
    body.SetAngularVelocity({0.1f, 0.2f, 0.3f});
    Spot::Scene restored;
    REQUIRE(restored.LoadFromString(scene.SaveToString()));
    const auto* restoredRenderer = restored.FindComponent<Spot::MeshRenderer>();
    REQUIRE(restoredRenderer);
    REQUIRE(restoredRenderer->GetMaterial());
    CHECK(*restoredRenderer->GetMaterial() == material);
    const auto* restoredBody = restored.FindComponent<Spot::Rigidbody>();
    REQUIRE(restoredBody);
    CHECK(restoredBody->GetLinearVelocity() == Spot::Vector3{1.0f, 2.0f, 3.0f});
    CHECK(restoredBody->GetAngularVelocity() == Spot::Vector3{0.1f, 0.2f, 0.3f});
}

TEST_CASE("Save data round-trips Archive fields and replaces an existing slot", "[gameplay][save]") {
    TempDirectory directory;
    Spot::SaveData saves(directory.GetPath() / "saves");
    int score = 42;
    std::string name = "Player";
    Spot::Vector3 position{1.0f, 2.0f, 3.0f};
    std::vector<int> inventory{2, 5, 9};
    const auto serialize = [&](Spot::Archive& archive, std::uint32_t) {
        archive.Field("score", score);
        archive.Field("name", name);
        archive.Field("position", position);
        archive.Field("inventory", inventory);
    };
    REQUIRE(saves.Save("slot_\xC3\xA9.json", 2, serialize));
    score = 99;
    REQUIRE(saves.Save("slot_\xC3\xA9.json", 2, serialize));
    score = 0;
    name.clear();
    position = {};
    inventory.clear();
    const auto loaded = saves.Load("slot_\xC3\xA9.json", 2, serialize);
    REQUIRE(loaded);
    CHECK(loaded.version == 2);
    CHECK(score == 99);
    CHECK(name == "Player");
    CHECK(position == Spot::Vector3{1.0f, 2.0f, 3.0f});
    CHECK(inventory == std::vector<int>{2, 5, 9});
    CHECK(std::distance(std::filesystem::directory_iterator(saves.GetDirectory()),
                        std::filesystem::directory_iterator()) == 1);
}

TEST_CASE("Save data exposes older versions for migration and rejects newer versions before loading",
          "[gameplay][save]") {
    TempDirectory directory;
    Spot::SaveData saves(directory.GetPath());
    int oldScore = 5;
    REQUIRE(
        saves.Save("old.json", 0, [&](Spot::Archive& archive, std::uint32_t) { archive.Field("points", oldScore); }));
    int score = 0;
    const auto migrated = saves.Load("old.json", 1, [&](Spot::Archive& archive, std::uint32_t version) {
        CHECK(version == 0);
        archive.Field("points", score);
        score *= 10;
    });
    REQUIRE(migrated);
    CHECK(score == 50);
    REQUIRE(
        saves.Save("future.json", 3, [&](Spot::Archive& archive, std::uint32_t) { archive.Field("score", score); }));
    const auto future = saves.Load("future.json", 2, [&](Spot::Archive&, std::uint32_t) { score = -1; });
    CHECK(future.status == Spot::SaveDataStatus::UnsupportedVersion);
    CHECK(future.version == 3);
    CHECK(score == 50);
}

TEST_CASE("Save data rejects malformed files and names without calling the serializer", "[gameplay][save]") {
    TempDirectory directory;
    Spot::SaveData saves(directory.GetPath());
    int calls = 0;
    const auto serialize = [&](Spot::Archive&, std::uint32_t) { ++calls; };
    CHECK(saves.Load("missing.json", 1, serialize).status == Spot::SaveDataStatus::NotFound);
    for (const auto name : {"", ".", "..", "../escape.json", "sub/slot.json", "sub\\slot.json", "C:slot.json"}) {
        CHECK(saves.Save(name, 1, serialize).status == Spot::SaveDataStatus::InvalidName);
        CHECK(saves.Load(name, 1, serialize).status == Spot::SaveDataStatus::InvalidName);
    }
    for (const auto text : {"broken", "[]", "{}", R"({"format":"spot-save","version":-1,"data":{}})",
                            R"({"format":"spot-save","version":1,"data":[]})"}) {
        std::ofstream(directory.GetPath() / "invalid.json") << text;
        CHECK(saves.Load("invalid.json", 1, serialize).status == Spot::SaveDataStatus::InvalidFile);
    }
    CHECK(calls == 0);
}

TEST_CASE("Failed saves retain the previous file and clean temporary files", "[gameplay][save]") {
    TempDirectory directory;
    Spot::SaveData saves(directory.GetPath());
    int score = 7;
    const auto serialize = [&](Spot::Archive& archive, std::uint32_t) { archive.Field("score", score); };
    REQUIRE(saves.Save("slot.json", 1, serialize));
    const auto fail = [](Spot::Archive&, std::uint32_t) { throw std::runtime_error("save failure"); };
    CHECK(saves.Save("slot.json", 1, fail).status == Spot::SaveDataStatus::SerializationError);
    score = 0;
    REQUIRE(saves.Load("slot.json", 1, serialize));
    CHECK(score == 7);
    CHECK(saves.Load("slot.json", 1, fail).status == Spot::SaveDataStatus::SerializationError);
    std::filesystem::create_directory(directory.GetPath() / "blocked.json");
    CHECK(saves.Save("blocked.json", 1, serialize).status == Spot::SaveDataStatus::IoError);
    CHECK(std::filesystem::is_directory(directory.GetPath() / "blocked.json"));
    CHECK(std::distance(std::filesystem::directory_iterator(saves.GetDirectory()),
                        std::filesystem::directory_iterator()) == 2);
}

TEST_CASE("Removing or destroying unstarted physics components releases bodies created by queries",
          "[physics][lifecycle]") {
    Spot::Scene scene;
    auto& prop = scene.CreateEntity("Unstarted prop");
    prop.AddComponent<Spot::BoxCollider>().SetSize({1.0f, 1.0f, 1.0f});
    auto& body = prop.AddComponent<Spot::Rigidbody>();
    body.SetMass(2.0f);
    CHECK(body.GetLinearVelocity() == Spot::Vector3{});
    REQUIRE(scene.GetPhysics().GetBodyCount() == 1);
    REQUIRE(prop.RemoveComponent<Spot::Rigidbody>());
    REQUIRE(prop.RemoveComponent<Spot::Collider>());
    scene.Update(Spot::Scene::DefaultFixedTimeStep);
    CHECK(scene.GetPhysics().GetBodyCount() == 0);

    auto& doomed = scene.CreateEntity("Unstarted doomed prop");
    doomed.AddComponent<Spot::BoxCollider>().SetSize({1.0f, 1.0f, 1.0f});
    auto& doomedBody = doomed.AddComponent<Spot::Rigidbody>();
    doomedBody.SetMass(2.0f);
    CHECK(doomedBody.GetLinearVelocity() == Spot::Vector3{});
    REQUIRE(scene.GetPhysics().GetBodyCount() == 1);
    doomed.Destroy();
    scene.Update(Spot::Scene::DefaultFixedTimeStep);
    CHECK(scene.GetPhysics().GetBodyCount() == 0);
}
