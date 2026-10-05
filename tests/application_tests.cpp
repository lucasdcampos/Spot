#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Observed {
    std::vector<std::string> calls;
    std::string windowTitle;
    bool hasAudioOutput = true;
    float smallestDelta = 1.0f;
};

// Records its callbacks and quits after a fixed number of frames on every run.
class RecordingApplication final : public Spot::Application {
public:
    RecordingApplication(Spot::ApplicationConfig config, Observed& observed, int frames)
        : Application(std::move(config)), m_observed(observed), m_framesPerRun(frames) {}

protected:
    void OnStart() override {
        m_observed.calls.emplace_back("start");
        m_framesLeft = m_framesPerRun;
        m_observed.windowTitle = GetWindow().GetTitle();
        m_observed.hasAudioOutput = GetAudioDevice().HasOutput();
        Spot::Log::Info("started");
    }

    void OnUpdate(float deltaTime) override {
        m_observed.calls.emplace_back("update");
        m_observed.smallestDelta = std::min(m_observed.smallestDelta, deltaTime);
        if (--m_framesLeft == 0) {
            Quit();
        }
    }

    void OnStop() override {
        m_observed.calls.emplace_back("stop");
        CHECK_FALSE(IsRunning());
    }

private:
    Observed& m_observed;
    int m_framesPerRun;
    int m_framesLeft = 0;
};

Spot::ApplicationConfig MakeConfig() {
    return {.window = {.title = "Application Tests", .width = 320, .height = 240},
            .graphics = {.enabled = false},
            .audio = {.enabled = false}};
}

} // namespace

TEST_CASE("Application runs frames between start and stop until Quit is called", "[application]") {
    Observed observed;
    RecordingApplication application(MakeConfig(), observed, 3);

    CHECK(application.Run() == EXIT_SUCCESS);

    CHECK(observed.calls == std::vector<std::string>{"start", "update", "update", "update", "stop"});
    CHECK(observed.windowTitle == "Application Tests");
    CHECK_FALSE(observed.hasAudioOutput);
    CHECK(observed.smallestDelta >= 0.0f);
    CHECK_FALSE(application.IsRunning());
}

TEST_CASE("Application can run again after it stopped", "[application]") {
    Observed observed;
    RecordingApplication application(MakeConfig(), observed, 1);

    CHECK(application.Run() == EXIT_SUCCESS);
    CHECK(application.Run() == EXIT_SUCCESS);

    CHECK(observed.calls == std::vector<std::string>{"start", "update", "stop", "start", "update", "stop"});
}

TEST_CASE("Application writes the log to the configured file while it exists", "[application]") {
    const auto directory = std::filesystem::temp_directory_path() / "spot_tests" / "application";
    const auto path = directory / "app.log";
    std::filesystem::remove_all(directory);
    const Spot::Tests::ScopedSinks sinks({});

    {
        Observed observed;
        auto config = MakeConfig();
        config.logFile = path;
        RecordingApplication application(std::move(config), observed, 1);
        application.Run();
    }
    Spot::Log::Info("after the application is gone");

    std::ifstream file(path);
    const std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();
    CHECK_THAT(contents, Catch::Matchers::EndsWith("[App] started\n"));

    std::filesystem::remove_all(directory);
}
