#include "log_capture.h"
#include "temp_directory.h"
#include <spot/scene/physics/physics_world.h>

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
        CHECK(GetApplication().GetScene() == this);
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

class RetainedComponent final : public Spot::Component {
public:
    int starts = 0;
    int updates = 0;
    void OnStart() override { ++starts; }
    void OnUpdate(float) override { ++updates; }
};

// Retained children detach, carry their descendants, and keep their identity and component state.
class RetainingApplication final : public Spot::Application {
public:
    explicit RetainingApplication(Spot::ApplicationConfig config) : Application(std::move(config)) {}

protected:
    void OnStart() override { LoadScene<Spot::Scene>(); }
    void OnUpdate(float) override {
        if (++m_frame == 1) {
            auto& parent = GetScene()->CreateEntity("Temporary");
            parent.GetTransform().SetPosition({3, 0, 0});
            auto& kept = parent.CreateChild("Player");
            kept.GetTransform().SetPosition({2, 0, 0});
            kept.SetKeepOnSceneChange();
            m_component = &kept.AddComponent<RetainedComponent>();
            auto& music = kept.AddComponent<Spot::AudioSource>();
            music.sound = std::make_shared<Spot::Sound>(48000, 1, std::vector<float>(48000, 0.1f));
            music.loop = true;
            music.spatial = false;
            kept.CreateChild("Inventory").SetKeepOnSceneChange();
            m_player = kept.GetId();
            LoadScene<Spot::Scene>();
        } else if (m_frame == 2) {
            auto* player = GetScene()->GetEntity(m_player);
            REQUIRE(player);
            CHECK(player->GetParent() == nullptr);
            CHECK(player->GetComponent<RetainedComponent>() == m_component);
            CHECK(m_component->starts == 1);
            CHECK(m_component->updates == 1);
            CHECK(player->GetComponent<Spot::AudioSource>()->IsPlaying());
            CHECK(GetScene()->GetSystem<Spot::AudioSystem>());
            CHECK(player->GetTransform().GetPosition().x == 5.0f);
            CHECK(player->FindChild("Inventory"));
            CHECK(GetScene()->FindEntity("Temporary") == nullptr);
            CHECK(GetScene()->GetEntityCount() == 2);
            LoadScene<Spot::Scene>();
        } else if (m_frame == 3) {
            REQUIRE(GetScene()->GetEntity(m_player));
            CHECK(m_component->starts == 1);
            CHECK(m_component->updates == 2);
            CHECK(GetScene()->GetEntity(m_player)->GetComponent<Spot::AudioSource>()->IsPlaying());
            LoadScene(nullptr);
        } else {
            CHECK(GetScene() == nullptr);
            Quit();
        }
    }

private:
    RetainedComponent* m_component = nullptr;
    Spot::EntityId m_player = Spot::EntityId::Invalid;
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

namespace {

class RecordingModule : public Spot::Module {
public:
    RecordingModule(std::vector<std::string>& calls, std::string label, bool consumes = false)
        : m_calls(calls), m_label(std::move(label)), m_consumes(consumes) {}

protected:
    void OnStart() override { m_calls.push_back(m_label + ".start"); }
    void OnStop() override { m_calls.push_back(m_label + ".stop"); }
    bool OnEvent(const Spot::Event& event) override {
        if (event.Is<Spot::KeyPressedEvent>())
            m_calls.push_back(m_label + ".event");
        return m_consumes && event.Is<Spot::KeyPressedEvent>();
    }
    void OnUpdate(float) override { m_calls.push_back(m_label + ".update"); }

private:
    std::vector<std::string>& m_calls;
    std::string m_label;
    bool m_consumes;
};

class FirstModule final : public RecordingModule {
    using RecordingModule::RecordingModule;
};
class SecondModule final : public RecordingModule {
    using RecordingModule::RecordingModule;
};

class ModuleApplication final : public Spot::Application {
public:
    explicit ModuleApplication(std::vector<std::string>& calls) : Application(MakeConfig()), m_calls(calls) {}

protected:
    void OnStart() override { m_calls.emplace_back("app.start"); }
    void OnUpdate(float) override {
        m_calls.emplace_back("app.update");
        DispatchEvent(Spot::Event(Spot::KeyPressedEvent{.key = Spot::Key::A, .repeat = false}));
        m_calls.emplace_back(GetInputState().IsKeyDown(Spot::Key::A) ? "key.down" : "key.up");
        Quit();
    }
    void OnStop() override { m_calls.emplace_back("app.stop"); }

private:
    std::vector<std::string>& m_calls;
};

} // namespace

TEST_CASE("Application starts its modules in order, stops them in reverse and calls their hooks", "[application]") {
    std::vector<std::string> calls;
    ModuleApplication application(calls);
    auto& first = application.AddModule<FirstModule>(calls, "first");
    application.AddModule<SecondModule>(calls, "second", true);

    CHECK(application.GetModule<FirstModule>() == &first);
    CHECK(&first.GetApplication() == &application);
    CHECK(application.GetModule<RecordingModule>() == nullptr);
    CHECK(application.Run() == EXIT_SUCCESS);

    CHECK(calls == std::vector<std::string>{"first.start", "second.start", "app.start", "app.update", "first.event",
                                            "second.event", "key.up", "first.update", "second.update", "app.stop",
                                            "second.stop", "first.stop"});
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

TEST_CASE("Application retains entity subtrees through replacements and destroys them on explicit unload",
          "[application]") {
    auto config = MakeConfig();
    config.audio.manualMix = true;
    RetainingApplication application(config);
    CHECK(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetScene() == nullptr);
}

namespace {

// Locks the cursor, as games do, then unlocks it, then captures the mouse itself, recording the window's capture after
// each, and ends as games do.
class CursorApplication final : public Spot::Application {
public:
    CursorApplication(Spot::ApplicationConfig config, std::vector<bool>& captured)
        : Application(std::move(config)), m_captured(captured) {}

protected:
    void OnUpdate(float /*deltaTime*/) override {
        switch (++m_frame) {
        case 1:
            Spot::Input::SetCursorLocked(true);
            break;
        case 2:
            m_captured.push_back(GetWindow().IsMouseCaptured());
            Spot::Input::SetCursorLocked(false);
            break;
        case 3:
            m_captured.push_back(GetWindow().IsMouseCaptured());
            m_capturable = GetWindow().SetMouseCaptured(true);
            break;
        default:
            m_captured.push_back(GetWindow().IsMouseCaptured() == m_capturable);
            QuitGame();
            break;
        }
    }

private:
    std::vector<bool>& m_captured;
    int m_frame = 0;
    bool m_capturable = false;
};

} // namespace

TEST_CASE("Application captures the mouse while the game locks the cursor, and quits when the game does",
          "[application]") {
    std::vector<bool> captured;
    CursorApplication application(MakeConfig(), captured);
    CHECK(application.Run() == EXIT_SUCCESS);

    REQUIRE(captured.size() == 3);
    CHECK(captured[0]);
    CHECK_FALSE(captured[1]);
    // The application captured the mouse itself, which the lock left alone since it did not change.
    CHECK(captured[2]);
}

TEST_CASE("Game settings are readable JSON that loads back", "[application]") {
    const Spot::GameSettings settings{.name = "Café",
                                      .version = "1.2.3",
                                      .startupScene = Spot::AssetId::Generate(),
                                      .importDirectory = "data/imported"};
    const std::string text = settings.SaveToString();
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"format\": \"spot-game\""));

    const auto loaded = Spot::GameSettings::LoadFromString(text);
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "Café");
    CHECK(loaded->version == "1.2.3");
    CHECK(loaded->startupScene == settings.startupScene);
    CHECK(loaded->importDirectory == "data/imported");

    CHECK_FALSE(Spot::GameSettings::LoadFromString("{\"format\": \"spot-project\"}").has_value());
    CHECK_FALSE(Spot::GameSettings::LoadFromString("nope").has_value());
}

TEST_CASE("Game settings keep their sections, and the ones this build does not know", "[application]") {
    Spot::GameSettings settings{.name = "Sections"};
    settings.sections.physics.gravity = {0.0f, -3.0f, 1.0f};
    settings.sections.physics.fixedTimeStep = 0.02f;
    settings.sections.unknown["future"] = R"({"answer":42})";

    const auto loaded = Spot::GameSettings::LoadFromString(settings.SaveToString());
    REQUIRE(loaded.has_value());
    CHECK(loaded->sections.physics.gravity == Spot::Vector3{0.0f, -3.0f, 1.0f});
    CHECK(loaded->sections.physics.fixedTimeStep == 0.02f);
    REQUIRE(loaded->sections.unknown.contains("future"));
    CHECK(loaded->sections.unknown.at("future") == R"({"answer":42})");
    CHECK_FALSE(loaded->sections.unknown.contains("physics"));

    // Settings without sections, as older games have, get the defaults.
    const auto older = Spot::GameSettings::LoadFromString(R"({"format": "spot-game", "formatVersion": 1})");
    REQUIRE(older.has_value());
    CHECK(older->sections.physics.gravity == Spot::PhysicsWorld::DefaultGravity);

    // Fields a section does not have keep their values.
    Spot::SettingsSections sections;
    CHECK(sections.LoadFromString(R"({"physics": {"fixedTimeStep": 0.5}})"));
    CHECK(sections.physics.fixedTimeStep == 0.5f);
    CHECK(sections.physics.gravity == Spot::PhysicsWorld::DefaultGravity);
    CHECK_FALSE(sections.LoadFromString("[]"));
}

TEST_CASE("The rendering settings keep the look and quality, and give them to scene renderers", "[application]") {
    Spot::SettingsSections sections;
    sections.rendering.tonemapping = {.op = Spot::TonemapOperator::AgX, .exposure = 1.5f};
    sections.rendering.bloom.intensity = 0.0f;
    sections.rendering.grading.vignette = 0.5f;
    sections.rendering.fxaa = true;
    sections.rendering.msaaSamples = 2;
    sections.rendering.shadowDistance = 120.0f;
    Spot::SettingsSections loaded;
    REQUIRE(loaded.LoadFromString(sections.SaveToString()));
    CHECK(loaded.rendering.tonemapping.op == Spot::TonemapOperator::AgX);
    CHECK(loaded.rendering.tonemapping.exposure == 1.5f);
    CHECK(loaded.rendering.bloom.intensity == 0.0f);
    CHECK(loaded.rendering.grading.vignette == 0.5f);
    CHECK(loaded.rendering.fxaa);

    Spot::SceneRendererConfig config;
    loaded.rendering.Apply(config);
    CHECK(config.tonemapping.op == Spot::TonemapOperator::AgX);
    CHECK(config.fxaa);
    CHECK(config.sampleCount == 2);
    CHECK(config.shadowDistance == 120.0f);
    CHECK(config.grading.lut == nullptr);
}

TEST_CASE("Settings sections apply to scenes, which create physics only when it needs them", "[application]") {
    Spot::SettingsSections sections;
    sections.physics.fixedTimeStep = 0.25f;
    Spot::Scene scene;
    sections.Apply(scene);
    CHECK(scene.GetFixedTimeStep() == 0.25f);
    CHECK(scene.GetSystem<Spot::PhysicsWorld>() == nullptr);

    sections.physics.gravity = {0.0f, -1.0f, 0.0f};
    sections.Apply(scene);
    REQUIRE(scene.GetSystem<Spot::PhysicsWorld>() != nullptr);
    CHECK(scene.GetOrAddSystem<Spot::PhysicsWorld>().GetGravity() == Spot::Vector3{0.0f, -1.0f, 0.0f});
}

namespace {

// Records the settings its scenes get, loading a second scene after switching games.
class SettingsApplication final : public Spot::Application {
public:
    using Application::Application;

    std::vector<float> fixedTimeSteps;
    std::vector<bool> hasJump;

protected:
    void OnStart() override { LoadScene<Spot::Scene>(); }

    void OnUpdate(float /*deltaTime*/) override {
        fixedTimeSteps.push_back(GetScene()->GetFixedTimeStep());
        hasJump.push_back(Spot::Input::GetActionMap() == &GetInputActions() && GetInputActions().Find("Jump"));
        if (fixedTimeSteps.size() == 1) {
            Spot::GameSettings other = *GetGameSettings();
            other.sections.physics.fixedTimeStep = 0.5f;
            other.sections.input = {};
            SetGameSettings(other);
            LoadScene<Spot::Scene>();
        } else {
            Quit();
        }
    }
};

} // namespace

TEST_CASE("Applications apply their game's sections to the scenes they load", "[application]") {
    const TempDirectory temp;
    Spot::ApplicationConfig config = MakeConfig();
    config.logFile = temp.GetPath() / "game.log";
    config.game = Spot::GameSettings{.name = "Settings"};
    config.game->sections.physics.fixedTimeStep = 0.125f;
    config.game->sections.input = Spot::InputSettings::MakeDefault();
    config.userDataDirectory = temp.GetPath() / "user";

    SettingsApplication application(std::move(config));
    CHECK(application.Run() == EXIT_SUCCESS);
    CHECK(application.fixedTimeSteps == std::vector<float>{0.125f, 0.5f});
    // The game's actions too, replaced by the other game's.
    CHECK(application.hasJump == std::vector<bool>{true, false});
}

namespace {

// Rebinds Jump on its first run, and records Jump's binding on each.
class RebindingApplication final : public Spot::Application {
public:
    using Application::Application;

    bool rebind = false;
    Spot::InputControl jump;
    bool saved = false;

protected:
    void OnStart() override {
        jump = GetInputActions().Find("Jump")->bindings[0].control;
        if (rebind) {
            GetInputActions().Rebind("Jump", 0, Spot::InputControl::Of(Spot::Key::J));
            saved = SaveInputRebinds();
        }
        Quit();
    }
};

} // namespace

TEST_CASE("Applications load the player's rebinds from the user data folder", "[application][input]") {
    const TempDirectory temp;
    const auto run = [&](bool rebind) {
        Spot::ApplicationConfig config = MakeConfig();
        config.logFile = temp.GetPath() / "game.log";
        config.userDataDirectory = temp.GetPath() / "user";
        config.game = Spot::GameSettings{.name = "Rebinding"};
        config.game->sections.input = Spot::InputSettings::MakeDefault();
        RebindingApplication application(std::move(config));
        application.rebind = rebind;
        CHECK(application.Run() == EXIT_SUCCESS);
        return std::pair(application.jump, application.saved);
    };

    CHECK(run(true) == std::pair(Spot::InputControl::Of(Spot::Key::Space), true));
    CHECK(std::filesystem::is_regular_file(temp.GetPath() / "user" / Spot::Application::InputRebindsFileName));
    CHECK(run(false).first == Spot::InputControl::Of(Spot::Key::J));
}

TEST_CASE("Scene changes from lifecycle hooks wait until the following frame", "[application]") {
    struct RedirectScene final : Spot::Scene {
        explicit RedirectScene(int& updateCount) : updates(updateCount) {}
        int& updates;
        void OnLoad() override {
            // The second queued request supersedes the first.
            GetApplication().LoadScene<Spot::Scene>();
            GetApplication().LoadScene(nullptr);
            CHECK(GetApplication().GetScene() == this);
        }
        void OnUpdate(float) override { ++updates; }
    };
    struct App final : Spot::Application {
        explicit App(int& updateCount) : Application(MakeConfig()), updates(updateCount) {}
        int& updates;
        int frames = 0;
        void OnStart() override { LoadScene<RedirectScene>(updates); }
        void OnUpdate(float) override {
            if (++frames == 1) {
                CHECK(GetScene());
            } else {
                CHECK(GetScene() == nullptr);
                Quit();
            }
        }
    };
    int updates = 0;
    App app(updates);
    CHECK(app.Run() == EXIT_SUCCESS);
    CHECK(updates == 1);
}
