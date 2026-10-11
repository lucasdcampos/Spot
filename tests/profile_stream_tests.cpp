#include "core/profile_stream.h"
#include <spot.h>

#include "log_capture.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Profile frames round-trip over the existing log stream with stable names", "[profiler][stream]") {
    Spot::ProfileFrame frame;
    frame.number = 42;
    frame.duration = std::chrono::milliseconds(15);
    frame.droppedSamples = 3;
    frame.samples = {{"Update", 0, std::chrono::milliseconds(1), std::chrono::milliseconds(8)},
                     {"Physics", 1, std::chrono::milliseconds(2), std::chrono::milliseconds(5)}};
    frame.counters = {{"Bodies", 500}};
    auto wire = Spot::EncodeProfileFrame(frame);
    const auto decoded = Spot::DecodeProfileFrame("[14:03:12.345] [INFO ] [Spot] " + wire);
    REQUIRE(decoded);
    wire.clear();
    CHECK(decoded->number == frame.number);
    CHECK(decoded->duration == frame.duration);
    CHECK(decoded->droppedSamples == 3);
    REQUIRE(decoded->samples.size() == 2);
    CHECK(decoded->samples[1].name == "Physics");
    CHECK(decoded->samples[1].depth == 1);
    CHECK(decoded->samples[1].duration == std::chrono::milliseconds(5));
    CHECK(Spot::Profiler::GetCounter(*decoded, "Bodies") == 500);
}

TEST_CASE("Malformed profile records are ignored instead of entering profiler history", "[profiler][stream]") {
    for (
        auto line :
        {"hello", "@spot-profile {}", "@spot-profile []", "@spot-profile invalid",
         R"(@spot-profile {"version":2,"number":1,"duration":10,"dropped":0,"samples":[],"counters":[]})",
         R"(@spot-profile {"version":1,"number":1,"duration":-1,"dropped":0,"samples":[],"counters":[]})",
         R"(@spot-profile {"version":1,"number":1,"duration":10,"dropped":0,"samples":[["Bad",-1,0,1]],"counters":[]})",
         R"(@spot-profile {"version":1,"number":1,"duration":10,"dropped":0,"samples":[["Bad",0,-1,1]],"counters":[]})"}) {
        CHECK_FALSE(Spot::DecodeProfileFrame(line));
    }
    CHECK_FALSE(Spot::DecodeProfileFrame(std::string(4 * 1024 * 1024 + 1, 'x')));
}

TEST_CASE("Application streams its first completed frame and shutdown frame exactly once", "[profiler][stream]") {
    class ProfileApplication final : public Spot::Application {
    public:
        ProfileApplication() : Application({.graphics = {.enabled = false}, .audio = {.enabled = false}}) {
            SetProfileStreamEnabled(true);
        }

    protected:
        void OnUpdate(float) override {
            SPOT_PROFILE_SCOPE("Profile fixture update");
            Spot::Profiler::Count("Fixture", 3);
            if (++m_updates == 2) {
                Quit();
            }
        }

    private:
        int m_updates = 0;
    };
    Spot::Profiler::Clear();
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks({sink});
    ProfileApplication application;
    REQUIRE(application.Run() == 0);
    std::vector<Spot::ProfileFrame> frames;
    for (const auto& record : sink->GetRecords()) {
        if (auto frame = Spot::DecodeProfileFrame(record.message)) {
            frames.push_back(std::move(*frame));
        }
    }
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].number == 0);
    CHECK(frames[1].number == 1);
    CHECK(Spot::Profiler::GetCounter(frames[0], "Fixture") == 3);
    CHECK(Spot::Profiler::GetCounter(frames[1], "Fixture") == 3);
    CHECK_FALSE(frames[1].samples.empty());
    Spot::Profiler::Clear();
}
