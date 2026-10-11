#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>

using Catch::Approx;
using namespace std::chrono_literals;

namespace {

class StepCounter final : public Spot::Component {
public:
    int count = 0;

protected:
    void OnFixedUpdate(float) override { ++count; }
};

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
