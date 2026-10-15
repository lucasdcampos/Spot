#include "runtime/runtime_stats.h"
#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("Runtime statistics combine frame counters and use unscaled measured frame durations", "[stats]") {
    Spot::ProfileFrame frame;
    frame.duration = std::chrono::milliseconds(10);
    frame.counters = {{"Draw calls", 2},
                      {"2D draw calls", 1},
                      {"Shadow draw calls", 3},
                      {"Triangles", 40},
                      {"2D triangles", 2},
                      {"Bodies", 8},
                      {"Voices", 4},
                      {"Resident memory", 1024 * 1024}};
    const auto stats = Spot::RuntimeStats::FromFrame(frame);
    CHECK(stats.fps == Catch::Approx(100.0));
    CHECK(stats.frameMilliseconds == Catch::Approx(10.0));
    CHECK(stats.drawCalls == 6);
    CHECK(stats.triangles == 42);
    CHECK(stats.bodies == 8);
    CHECK(stats.voices == 4);
    CHECK(stats.memoryBytes == 1024 * 1024);
    CHECK(stats.Format().find("1.0 MiB resident") != std::string::npos);
    frame.duration = {};
    frame.counters = {{"Draw calls", std::numeric_limits<double>::infinity()}, {"Bodies", -1}};
    const auto empty = Spot::RuntimeStats::FromFrame(frame);
    CHECK(empty.fps == 0.0);
    CHECK(empty.drawCalls == 0);
    CHECK(empty.bodies == 0);
}

TEST_CASE("Runtime statistics sample live bodies, silent audio voices and resident memory without a GPU", "[stats]") {
    Spot::Scene scene;
    auto& prop = scene.CreateEntity();
    prop.AddComponent<Spot::BoxCollider>();
    prop.AddComponent<Spot::Rigidbody>();
    scene.Update(0.02f);
    Spot::AudioDevice audio({.manualMix = true});
    Spot::Scene* scenes[] = {&scene};
    const auto stats = Spot::RuntimeStats::Sample(scenes, &audio);
    CHECK(stats.bodies == 1);
    CHECK(stats.voices == 0);
    CHECK(stats.memoryBytes > 0);
    CHECK(Spot::Process::GetResidentMemoryBytes() > 0);
}

TEST_CASE("Runtime FPS remains unscaled while simulation is paused and recording is disabled", "[stats]") {
    const bool enabled = Spot::Profiler::IsEnabled();
    const float scale = Spot::Time::GetTimeScale();
    Spot::FrameClock clock;
    Spot::Time::SetClock(&clock);
    Spot::Time::SetTimeScale(0.0f);
    clock.Start({});
    clock.Tick(Spot::FrameClock::TimePoint{} + std::chrono::milliseconds(50));
    Spot::Profiler::SetEnabled(false);
    const auto stats = Spot::RuntimeStats::Sample({});
    CHECK(stats.fps == Catch::Approx(20.0));
    CHECK(stats.frameMilliseconds == Catch::Approx(50.0));
    CHECK(stats.drawCalls == 0);
    CHECK(Spot::Time::GetDeltaTime() == 0.0f);
    Spot::Time::SetClock(nullptr);
    Spot::Time::SetTimeScale(scale);
    Spot::Profiler::SetEnabled(enabled);
}
