#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <thread>

TEST_CASE("Managed game profiles travel over stdout without changing the parent's environment", "[profiler][process]") {
    const auto before = Spot::Process::GetEnvironmentValue("SPOT_PROFILE_STREAM");
    Spot::GameRunner game;
    const std::string arguments[] = {"--emit-profile"};
    REQUIRE(game.Start(SPOT_CRASH_APP, arguments));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (game.IsRunning() && std::chrono::steady_clock::now() < deadline) {
        game.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE_FALSE(game.IsRunning());
    game.Update();
    CHECK(game.GetExitCode() == 0);
    const auto frames = game.GetProfileFrames();
    REQUIRE(frames.size() == Spot::Profiler::MaxFrames);
    CHECK(frames.front()->number == 11);
    CHECK(frames.back()->number == 250);
    REQUIRE(frames.back()->samples.size() == 1);
    CHECK(frames.back()->samples[0].name == "Remote physics");
    CHECK(Spot::Profiler::GetCounter(*frames.back(), "Bodies") == 250);
    CHECK(Spot::Process::GetEnvironmentValue("SPOT_PROFILE_STREAM") == before);
}
