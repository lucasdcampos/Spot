#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using Spot::ProfileFrame;
namespace Profiler = Spot::Profiler;

namespace {

// Starts every test from no frames, and leaves none behind.
struct CleanProfiler {
    CleanProfiler() {
        Profiler::SetEnabled(true);
        Profiler::Clear();
    }
    ~CleanProfiler() {
        Profiler::SetEnabled(true);
        Profiler::Clear();
    }
};

void Leaf() {
    SPOT_PROFILE_SCOPE("Leaf");
}

void Branch() {
    SPOT_PROFILE_SCOPE("Branch");
    Leaf();
    Leaf();
}

} // namespace

TEST_CASE("Profile scopes nest into a tree of samples", "[profiler]") {
    const CleanProfiler clean;
    Profiler::BeginFrame();
    {
        SPOT_PROFILE_SCOPE("Root");
        Branch();
    }
    Leaf();
    Profiler::EndFrame();

    const auto frames = Profiler::GetFrames();
    REQUIRE(frames.size() == 1);
    const ProfileFrame& frame = *frames[0];
    REQUIRE(frame.samples.size() == 5);
    const std::vector<std::pair<std::string, std::uint32_t>> expected = {
        {"Root", 0}, {"Branch", 1}, {"Leaf", 2}, {"Leaf", 2}, {"Leaf", 0}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(std::string(frame.samples[i].name) == expected[i].first);
        CHECK(frame.samples[i].depth == expected[i].second);
    }
    // Children start no earlier and end no later than their parents.
    const auto end = [](const Spot::ProfileSample& sample) { return sample.start + sample.duration; };
    CHECK(frame.samples[1].start >= frame.samples[0].start);
    CHECK(end(frame.samples[1]) <= end(frame.samples[0]));
    CHECK(frame.samples[3].start >= end(frame.samples[2]));
    CHECK(end(frame.samples[4]) <= frame.duration);
}

TEST_CASE("Profile scopes belong to the frame they began in", "[profiler]") {
    const CleanProfiler clean;
    // Outside of frames, nothing is recorded.
    Leaf();
    CHECK(Profiler::GetFrames().empty());

    std::optional<Spot::Detail::ProfileScope> spanning;
    Profiler::BeginFrame();
    spanning.emplace("Spanning");
    Leaf();
    // Beginning a frame ends the one in progress, with its open scopes.
    Profiler::BeginFrame();
    Leaf();
    spanning.reset();
    Profiler::EndFrame();

    const auto frames = Profiler::GetFrames();
    REQUIRE(frames.size() == 2);
    CHECK(frames[0]->number + 1 == frames[1]->number);
    REQUIRE(frames[0]->samples.size() == 2);
    CHECK(std::string(frames[0]->samples[0].name) == "Spanning");
    CHECK(frames[0]->samples[0].start + frames[0]->samples[0].duration == frames[0]->duration);
    CHECK(frames[0]->samples[1].depth == 1);
    // The scope that ended in the next frame left it alone.
    REQUIRE(frames[1]->samples.size() == 1);
    CHECK(frames[1]->samples[0].depth == 0);
}

TEST_CASE("The profiler keeps the last frames in a ring", "[profiler]") {
    const CleanProfiler clean;
    const std::size_t total = Profiler::MaxFrames + 25;
    for (std::size_t i = 0; i < total; ++i) {
        Profiler::BeginFrame();
        Leaf();
        Profiler::EndFrame();
    }
    auto frames = Profiler::GetFrames();
    REQUIRE(frames.size() == Profiler::MaxFrames);
    CHECK(frames.front()->number == total - Profiler::MaxFrames);
    CHECK(frames.back()->number == total - 1);
    for (std::size_t i = 1; i < frames.size(); ++i) {
        CHECK(frames[i]->number == frames[i - 1]->number + 1);
    }

    // The frame in progress is not among them, and overwrites none of them.
    Profiler::BeginFrame();
    frames = Profiler::GetFrames();
    REQUIRE(frames.size() == Profiler::MaxFrames);
    CHECK(frames.back()->number == total - 1);
    CHECK(frames.back()->samples.size() == 1);
    Profiler::EndFrame();
    CHECK(Profiler::GetFrames().back()->number == total);
}

TEST_CASE("The profiler records other threads into the frame, and nothing while disabled", "[profiler]") {
    const CleanProfiler clean;
    Profiler::BeginFrame();
    Leaf();
    std::thread other(Leaf);
    other.join();
    Spot::JobSystem jobs(2);
    jobs.ParallelFor(4, 1, [](std::size_t, std::size_t) { Leaf(); });
    Profiler::EndFrame();
    REQUIRE(Profiler::GetFrames().size() == 1);
    const auto& samples = Profiler::GetFrames()[0]->samples;
    REQUIRE(samples.size() == 6);
    // The frame's own thread comes first, then the others, each numbered from 1. The frame's thread ran its own leaf
    // and the first range of the loop, and may have run more ranges while it waited.
    CHECK(std::ranges::count(samples, 0u, &Spot::ProfileSample::thread) >= 2);
    CHECK(std::ranges::count_if(samples, [](const auto& sample) { return sample.thread > 0; }) >= 1);
    CHECK(std::ranges::all_of(samples, [](const auto& sample) { return sample.depth == 0; }));
    CHECK(std::ranges::is_sorted(samples, {}, &Spot::ProfileSample::thread));

    // Scopes another thread recorded after the frame ended wait for no frame: they are dropped.
    std::thread late(Leaf);
    late.join();
    Profiler::BeginFrame();
    Profiler::EndFrame();
    CHECK(Profiler::GetFrames().back()->samples.empty());

    Profiler::SetEnabled(false);
    Profiler::BeginFrame();
    Leaf();
    Profiler::EndFrame();
    CHECK(Profiler::GetFrames().size() == 2);

    CHECK(Profiler::Intern(std::string("Dynamic")) == "Dynamic");
    CHECK(Profiler::Intern("Dynamic").data() == Profiler::Intern(std::string("Dynamic")).data());
}

TEST_CASE("Scenes profile their update, by stage and by system", "[profiler]") {
    const CleanProfiler clean;
    Spot::Scene scene;
    scene.CreateEntity().AddComponent<Spot::BoxCollider>();
    Profiler::BeginFrame();
    scene.Update(Spot::Scene::DefaultFixedTimeStep);
    Profiler::EndFrame();

    const auto frames = Profiler::GetFrames();
    REQUIRE(frames.size() == 1);
    std::vector<std::string> names;
    for (const Spot::ProfileSample& sample : frames[0]->samples) {
        names.emplace_back(sample.name);
    }
    CHECK(names.front() == "Scene::Update");
    const auto has = [&names](const std::string& name) { return std::ranges::find(names, name) != names.end(); };
    CHECK(has("Components::OnUpdate"));
    CHECK(has("Systems::OnFixedUpdate"));
    CHECK(has("Spot::PhysicsWorld"));
}

TEST_CASE("Profiler counters add up within a frame and start over each frame", "[profiler]") {
    const CleanProfiler clean;
    // Outside of frames, counts are dropped.
    Profiler::Count("Draws", 5.0);

    Profiler::BeginFrame();
    Profiler::Count("Draws", 2.0);
    Profiler::Count("Lights");
    Profiler::Count("Draws", 3.0);
    // Off the profiled thread, they are dropped too.
    std::thread([] { Profiler::Count("Draws", 100.0); }).join();
    Profiler::EndFrame();
    Profiler::BeginFrame();
    Profiler::Count("Lights");
    Profiler::EndFrame();

    const auto frames = Profiler::GetFrames();
    REQUIRE(frames.size() == 2);
    CHECK(Profiler::GetCounter(*frames[0], "Draws") == 5.0);
    CHECK(Profiler::GetCounter(*frames[0], "Lights") == 1.0);
    REQUIRE(frames[0]->counters.size() == 2);
    CHECK(frames[0]->counters[0].name == "Draws");
    CHECK(Profiler::GetCounter(*frames[1], "Draws") == 0.0);
    CHECK(Profiler::GetCounter(*frames[1], "Lights") == 1.0);
}
