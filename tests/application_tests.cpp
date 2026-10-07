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

class LoggingComponent final : public Spot::Component {
public:
    LoggingComponent(std::vector<std::string>& calls, std::string label) : m_calls(calls), m_label(std::move(label)) {}

protected:
    void OnStart() override { m_calls.push_back(m_label + ".start"); }
    void OnDestroy() override { m_calls.push_back(m_label + ".destroy"); }

private:
    std::vector<std::string>& m_calls;
    std::string m_label;
};

class LoggingScene final : public Spot::Scene {
public:
    LoggingScene(std::vector<std::string>& calls, std::string label) : m_calls(calls), m_label(std::move(label)) {}

protected:
    void OnLoad() override {
        m_calls.push_back(m_label + ".load");
        CHECK(IsLoaded());
        const auto scenes = GetApplication().GetScenes();
        CHECK(std::ranges::find(scenes, this) != scenes.end());
        CreateEntity().AddComponent<LoggingComponent>(m_calls, m_label);
    }
    void OnUnload() override { m_calls.push_back(m_label + ".unload"); }
    void OnUpdate(float /*deltaTime*/) override { m_calls.push_back(m_label + ".update"); }

private:
    std::vector<std::string>& m_calls;
    std::string m_label;
};

// Loads a scene on start, switches to another one on the second frame and quits on the third.
class SceneSwitchingApplication final : public Spot::Application {
public:
    SceneSwitchingApplication(Spot::ApplicationConfig config, std::vector<std::string>& calls)
        : Application(std::move(config)), m_calls(calls) {}

protected:
    void OnStart() override {
        LoadScene<LoggingScene>(m_calls, "first");
        // Scenes take over at the start of the next frame.
        CHECK(GetScene() == nullptr);
    }

    void OnUpdate(float /*deltaTime*/) override {
        m_calls.push_back("frame");
        if (++m_frame == 2) {
            LoadScene<LoggingScene>(m_calls, "second");
        } else if (m_frame == 3) {
            Quit();
        }
    }

    void OnStop() override { m_calls.emplace_back("stop"); }

private:
    std::vector<std::string>& m_calls;
    int m_frame = 0;
};

// Runs two scenes side by side, keeps an entity across scene changes and replaces everything on the third frame.
class MultiSceneApplication final : public Spot::Application {
public:
    MultiSceneApplication(Spot::ApplicationConfig config, std::vector<std::string>& calls)
        : Application(std::move(config)), m_calls(calls) {}

protected:
    void OnStart() override {
        m_base = &LoadScene<LoggingScene>(m_calls, "base");
        m_overlay = &LoadSceneAdditive<LoggingScene>(m_calls, "overlay");
        // Unloaded before it had a chance to load.
        UnloadScene(LoadSceneAdditive<LoggingScene>(m_calls, "dropped"));
    }

    void OnUpdate(float /*deltaTime*/) override {
        m_calls.push_back("frame");
        switch (++m_frame) {
        case 1: {
            CHECK(GetScenes() == std::vector<Spot::Scene*>{m_base, m_overlay});
            CHECK(GetScene() == m_base);
            Spot::Entity& player = m_base->CreateEntity("Player");
            player.AddComponent<LoggingComponent>(m_calls, "player");
            MakePersistent(player);
            UnloadScene(*m_base);
            break;
        }
        case 2:
            CHECK(GetScene() == m_overlay);
            LoadScene<LoggingScene>(m_calls, "next");
            break;
        default:
            CHECK(GetPersistentScene().FindEntity("Player") != nullptr);
            CHECK(GetScenes().size() == 1);
            Quit();
            break;
        }
    }

    void OnStop() override { m_calls.emplace_back("stop"); }

private:
    std::vector<std::string>& m_calls;
    Spot::Scene* m_base = nullptr;
    Spot::Scene* m_overlay = nullptr;
    int m_frame = 0;
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

TEST_CASE("Application switches scenes between frames and unloads the last one when it stops", "[application]") {
    std::vector<std::string> calls;
    SceneSwitchingApplication application(MakeConfig(), calls);

    CHECK(application.Run() == EXIT_SUCCESS);

    CHECK(calls == std::vector<std::string>{"first.load", "frame", "first.start", "first.update", "frame",
                                            "first.update", "first.unload", "first.destroy", "second.load", "frame",
                                            "second.start", "second.update", "stop", "second.unload",
                                            "second.destroy"});
    CHECK(application.GetScene() == nullptr);
}

namespace {

// Counts what a scene gets.
class CountingScene final : public Spot::Scene {
public:
    int updates = 0;
    int keys = 0;
    int resizes = 0;

    void OnUpdate(float /*deltaTime*/) override { ++updates; }
    void OnEvent(const Spot::Event& event) override {
        keys += event.Is<Spot::KeyPressedEvent>() ? 1 : 0;
        resizes += event.Is<Spot::WindowResizeEvent>() ? 1 : 0;
    }
};

// Pauses its scene, steps it, and keeps key presses from it, recording how many updates the scene had each frame.
class PausingApplication final : public Spot::Application {
public:
    explicit PausingApplication(std::vector<int>& updates) : Application(MakeConfig()), m_updates(updates) {}

    CountingScene* scene = nullptr;

protected:
    void OnStart() override { scene = &LoadScene<CountingScene>(); }

    void OnUpdate(float /*deltaTime*/) override {
        ++m_frame;
        if (m_frame > 1) {
            m_updates.push_back(scene->updates);
        }
        if (m_frame == 2) {
            SetScenesPaused(true);
            DispatchEvent(Spot::KeyPressedEvent{.key = Spot::Key::A, .repeat = false});
            DispatchEvent(Spot::WindowResizeEvent{.width = 10, .height = 10});
        }
        if (m_frame == 3) {
            // Events reach paused scenes, unless kept from them.
            CHECK(scene->keys == 0);
            CHECK(scene->resizes == 1);
        }
        if (m_frame == 4) {
            StepScenes();
        }
        if (m_frame == 6) {
            CHECK(AreScenesPaused());
            Quit();
        }
    }

    bool DeliversToScenes(const Spot::Event& event) override { return !event.Is<Spot::KeyPressedEvent>(); }

private:
    std::vector<int>& m_updates;
    int m_frame = 0;
};

} // namespace

TEST_CASE("Application pauses its scenes, steps them, and keeps events from them when told to", "[application]") {
    std::vector<int> updates;
    PausingApplication application(updates);
    CHECK(application.Run() == EXIT_SUCCESS);

    // Updates seen in frames 2 to 6: the scene updated in frame 1, was paused before frame 2's update, and stepped
    // once in frame 4.
    CHECK(updates == std::vector<int>{1, 1, 1, 2, 2});
}

TEST_CASE("Application runs scenes side by side and keeps persistent entities", "[application]") {
    std::vector<std::string> calls;
    MultiSceneApplication application(MakeConfig(), calls);

    CHECK(application.Run() == EXIT_SUCCESS);

    CHECK(calls == std::vector<std::string>{
                       // Frame 1: both scenes load and run; the player moves to the persistent scene, which updates
                       // first.
                       "base.load", "overlay.load", "frame", "player.start", "base.start", "base.update",
                       "overlay.start", "overlay.update",
                       // Frame 2: the base scene unloads, but the player was moved out of it.
                       "base.unload", "base.destroy", "frame", "overlay.update",
                       // Frame 3: everything is replaced by the next scene.
                       "overlay.unload", "overlay.destroy", "next.load", "frame", "next.start", "next.update",
                       // Stop: loaded scenes unload, then the persistent scene is cleared.
                       "stop", "next.unload", "next.destroy", "player.destroy"});
}
