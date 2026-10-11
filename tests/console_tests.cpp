#include <spot.h>

#include "temp_directory.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

TEST_CASE("Console parsing preserves quoted arguments and rejects incomplete input", "[console]") {
    std::vector<std::string> args;
    REQUIRE(Spot::Console::Parse(R"(spawn "two words" '' escaped\ space "a\"b")", args));
    CHECK(args == std::vector<std::string>{"spawn", "two words", "", "escaped space", "a\"b"});
    CHECK_FALSE(Spot::Console::Parse("spawn \"unfinished", args));
    CHECK(args.empty());
    CHECK_FALSE(Spot::Console::Parse("spawn trail\\", args));
    REQUIRE(Spot::Console::Parse(" \t\n", args));
    CHECK(args.empty());
}

TEST_CASE("Console registrations scope callbacks and allow mutation during execution", "[console]") {
    Spot::Console console;
    Spot::ConsoleRegistration registration;
    registration = console.Register({"one", "Removes itself", Spot::ConsoleFlags::None,
                                     [&](std::span<const std::string> args) -> Spot::ConsoleResult {
                                         CHECK(args.size() == 1);
                                         registration.Reset();
                                         return {true, args.front()};
                                     }});
    REQUIRE(registration.IsRegistered());
    CHECK_FALSE(console.Register({"one", {}, {}, [](auto) { return Spot::ConsoleResult{}; }}).IsRegistered());
    CHECK(console.Execute("one value").message == "value");
    CHECK_FALSE(registration.IsRegistered());
    CHECK_FALSE(console.Execute("one"));
    auto failing =
        console.Register({"fail", {}, {}, [](auto) -> Spot::ConsoleResult { throw std::runtime_error("failure"); }});
    CHECK(console.Execute("fail").message == "failure");
    {
        Spot::Console temporary;
        registration = temporary.Register({"owned", {}, {}, [](auto) { return Spot::ConsoleResult{}; }});
    }
    CHECK_FALSE(registration.IsRegistered());
    registration.Reset();
}

TEST_CASE("Typed cvars validate complete finite values, defaults and cheat flags", "[console]") {
    Spot::Console console;
    Spot::CVar<float> scale(console, "time.scale", 1.0f, "Simulation speed", Spot::ConsoleFlags::Cheat,
                            [](float value) { return value >= 0.0f && value <= 10.0f; });
    CHECK_FALSE(console.Execute("time.scale 0"));
    CHECK(scale.Get() == 1.0f);
    console.SetCheatsEnabled(true);
    REQUIRE(console.Execute("time.scale 0.5"));
    CHECK(scale.Get() == 0.5f);
    for (auto value : {"-1", "11", "nan", "inf", "1tail", "9999999999999999999999999999999999999999999999999"}) {
        CHECK_FALSE(console.Execute(std::string("time.scale ") + value));
        CHECK(scale.Get() == 0.5f);
    }
    CHECK(console.Execute("time.scale").message.find("default 1") != std::string::npos);
    Spot::CVar<bool> enabled(console, "enabled", false);
    REQUIRE(console.Execute("enabled true"));
    CHECK(enabled.Get());
    CHECK_FALSE(console.Execute("enabled maybe"));
    int limit = 3;
    Spot::CVar<int> bound(
        console, "limit", 3, "Bound setting", {}, [&] { return limit; }, [&](int value) { limit = value; });
    REQUIRE(console.Execute("limit 7"));
    CHECK(limit == 7);
    limit = 9;
    CHECK(bound.Get() == 9);
    CHECK_FALSE(console.Execute("limit 1.5"));
}

TEST_CASE("Console completion is sorted and history and output remain bounded", "[console]") {
    Spot::Console console;
    Spot::CVar<int> z(console, "render.z", 0);
    Spot::CVar<int> a(console, "render.a", 0);
    CHECK(console.Complete("render.") == std::vector<std::string>{"render.a", "render.z"});
    console.Execute("render.a 1");
    console.Execute("render.a 1");
    CHECK(console.GetHistory().size() == 1);
    console.Execute(" ");
    CHECK(console.GetHistory().size() == 1);
    for (int i = 0; i < 300; ++i) {
        console.Execute("render.a " + std::to_string(i));
    }
    CHECK(console.GetHistory().size() == Spot::Console::MaxHistory);
    CHECK(console.GetHistory().back() == "render.a 299");
    CHECK(console.GetOutput().size() == Spot::Console::MaxOutput);
}

TEST_CASE("Only saved cvars persist through Archive and SaveData", "[console]") {
    Spot::Console console;
    Spot::CVar<float> volume(console, "audio.volume", 1.0f, {}, Spot::ConsoleFlags::Saved);
    Spot::CVar<int> temporary(console, "temporary", 5);
    TempDirectory directory;
    Spot::SaveData saves(directory.GetPath());
    auto serialize = [&](Spot::Archive& archive, std::uint32_t) { console.Serialize(archive); };
    REQUIRE(volume.Set(0.25f));
    REQUIRE(saves.Save("console.json", 1, serialize));
    REQUIRE(volume.Set(0.8f));
    REQUIRE(temporary.Set(10));
    REQUIRE(saves.Load("console.json", 1, serialize));
    CHECK(volume.Get() == 0.25f);
    CHECK(temporary.Get() == 10);
}

TEST_CASE("Engine console bindings change public application, audio and time settings", "[console]") {
    Spot::Console console;
    Spot::Application application(
        {.graphics = {.enabled = false}, .audio = {.enabled = false}, .showCrashMessage = false});
    const auto previousScale = Spot::Time::GetTimeScale();
    {
        Spot::EngineConsole bindings(console, application);
        REQUIRE(console.Execute("console.cheats true"));
        REQUIRE(console.Execute("time.scale 0.25"));
        CHECK(Spot::Time::GetTimeScale() == 0.25f);
        REQUIRE(console.Execute("render.enabled false"));
        CHECK_FALSE(application.IsSceneRenderingEnabled());
        REQUIRE(console.Execute("render.max_fps 90"));
        CHECK(application.GetMaxFrameRate() == 90);
        CHECK_FALSE(console.Execute("render.max_fps -1"));
        CHECK_FALSE(console.Execute("audio.volume -1"));
        CHECK(console.Execute("help render.").message.find("render.max_fps") != std::string::npos);
    }
    CHECK(console.Complete("").empty());
    Spot::Time::SetTimeScale(previousScale);
}

TEST_CASE("Applications persist saved cvars and remove engine bindings when they stop", "[console]") {
    class SettingsApplication final : public Spot::Application {
    public:
        SettingsApplication(const std::filesystem::path& directory, bool write)
            : Application(
                  {.graphics = {.enabled = false}, .audio = {.enabled = false}, .userDataDirectory = directory}),
              m_write(write) {}

    protected:
        void OnStart() override {
            auto& console = Spot::Console::Get();
            if (m_write) {
                REQUIRE(console.Execute("render.max_fps 72"));
                REQUIRE(console.Execute("audio.volume 0.3"));
            } else {
                CHECK(GetMaxFrameRate() == 72);
                CHECK(GetAudioDevice().GetMasterVolume() == 0.3f);
            }
        }
        void OnUpdate(float) override { Quit(); }

    private:
        bool m_write;
    };
    TempDirectory directory;
    SettingsApplication first(directory.GetPath(), true);
    REQUIRE(first.Run() == EXIT_SUCCESS);
    CHECK(std::filesystem::is_regular_file(directory.GetPath() / "console.json"));
    CHECK(Spot::Console::Get().Complete("").empty());
    SettingsApplication second(directory.GetPath(), false);
    REQUIRE(second.Run() == EXIT_SUCCESS);
    CHECK(Spot::Console::Get().Complete("").empty());
}
