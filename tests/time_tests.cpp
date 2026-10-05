#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>

using namespace std::chrono_literals;

namespace {

const Spot::FrameClock::TimePoint Start{std::chrono::seconds(100)};

} // namespace

TEST_CASE("FrameClock measures the time between ticks", "[time]") {
    Spot::FrameClock clock;
    clock.Start(Start);

    clock.Tick(Start + 16ms);
    CHECK(clock.GetDeltaTime() == Catch::Approx(0.016f));
    CHECK(clock.GetTime() == Catch::Approx(0.016));
    CHECK(clock.GetFrameCount() == 1);

    clock.Tick(Start + 48ms);
    CHECK(clock.GetDeltaTime() == Catch::Approx(0.032f));
    CHECK(clock.GetTime() == Catch::Approx(0.048));
    CHECK(clock.GetFrameCount() == 2);
}

TEST_CASE("FrameClock clamps long frames", "[time]") {
    Spot::FrameClock clock(100ms);
    clock.Start(Start);

    clock.Tick(Start + 5s);

    CHECK(clock.GetDeltaTime() == Catch::Approx(0.1f));
    CHECK(clock.GetTime() == Catch::Approx(0.1));
}

TEST_CASE("FrameClock starts over when started again", "[time]") {
    Spot::FrameClock clock;
    clock.Start(Start);
    clock.Tick(Start + 10ms);

    clock.Start(Start + 1s);

    CHECK(clock.GetDeltaTime() == 0.0f);
    CHECK(clock.GetTime() == 0.0);
    CHECK(clock.GetFrameCount() == 0);
}

TEST_CASE("Unlimited FrameLimiter never waits", "[time]") {
    Spot::FrameLimiter limiter;

    CHECK(limiter.GetWaitTime(Start) == 0ns);
    CHECK(limiter.GetWaitTime(Start + 1ms) == 0ns);
}

TEST_CASE("FrameLimiter spaces frame starts by its period", "[time]") {
    Spot::FrameLimiter limiter(100);

    CHECK(limiter.GetWaitTime(Start) == 0ns);
    CHECK(limiter.GetWaitTime(Start + 3ms) == 7ms);
    // The wait above ends at 10ms, so the next frame is due at 20ms.
    CHECK(limiter.GetWaitTime(Start + 12ms) == 8ms);
}

TEST_CASE("FrameLimiter does not rush frames after a late one", "[time]") {
    Spot::FrameLimiter limiter(100);
    limiter.GetWaitTime(Start);

    CHECK(limiter.GetWaitTime(Start + 50ms) == 0ns);
    CHECK(limiter.GetWaitTime(Start + 52ms) == 8ms);
}

TEST_CASE("Time reads the clock it points at, and zero without one", "[time]") {
    Spot::FrameClock clock;
    clock.Start(Start);
    clock.Tick(Start + 20ms);

    CHECK(Spot::Time::GetFrameCount() == 0);

    Spot::Time::SetClock(&clock);
    CHECK(Spot::Time::GetDeltaTime() == Catch::Approx(0.02f));
    CHECK(Spot::Time::GetTime() == Catch::Approx(0.02));
    CHECK(Spot::Time::GetFrameCount() == 1);
    Spot::Time::SetClock(nullptr);

    CHECK(Spot::Time::GetDeltaTime() == 0.0f);
}

TEST_CASE("Application limits its frame rate and exposes its clock through Time", "[time][application]") {
    class LimitedApplication final : public Spot::Application {
    public:
        LimitedApplication()
            : Application({.window = {.title = "Time Tests"}, .graphics = {.enabled = false}, .maxFrameRate = 100}) {}

        std::uint64_t GetLastFrameCount() const { return m_lastFrameCount; }

    protected:
        void OnUpdate(float deltaTime) override {
            CHECK(deltaTime == Spot::Time::GetDeltaTime());
            m_lastFrameCount = Spot::Time::GetFrameCount();
            if (m_lastFrameCount == 5) {
                Quit();
            }
        }

    private:
        std::uint64_t m_lastFrameCount = 0;
    };

    LimitedApplication application;
    const auto begin = std::chrono::steady_clock::now();
    CHECK(application.Run() == EXIT_SUCCESS);
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    CHECK(application.GetLastFrameCount() == 5);
    // Five frames at 100 per second wait at least four periods between their starts.
    CHECK(elapsed >= 38ms);
    CHECK(Spot::Time::GetFrameCount() == 0);
}
