#include "spot/runtime/engine_console.h"

#include "spot/core/debug/profiler.h"
#include "spot/core/time/time.h"
#include "spot/runtime/application.h"

namespace Spot {
namespace {

template <typename T, typename Get, typename Set, typename Validate>
ConsoleRegistration Bind(Console& console, std::string name, T defaultValue, std::string description,
                         ConsoleFlags flags, Get get, Set set, Validate validate) {
    return console.RegisterVariable(
        std::move(name), std::move(description), flags, FormatConsoleValue(defaultValue),
        [get] { return FormatConsoleValue(get()); },
        [set, validate](std::string_view text) {
            T value{};
            if (!ParseConsoleValue(text, value) || !validate(value)) {
                return false;
            }
            set(value);
            return true;
        });
}

} // namespace

EngineConsole::EngineConsole(Console& console, Application& app) {
    m_registrations.push_back(console.Register({"help",
                                                "List commands matching an optional prefix",
                                                {},
                                                [&console](std::span<const std::string> args) -> ConsoleResult {
                                                    if (args.size() > 1) {
                                                        return {false, "Usage: help [prefix]"};
                                                    }
                                                    std::string text;
                                                    for (const auto& name :
                                                         console.Complete(args.empty() ? "" : args.front())) {
                                                        if (!text.empty()) {
                                                            text += '\n';
                                                        }
                                                        text += name;
                                                    }
                                                    return {true, text};
                                                }}));
    m_registrations.push_back(Bind<bool>(
        console, "console.cheats", false, "Allow development cheat commands", {},
        [&console] { return console.AreCheatsEnabled(); }, [&console](bool value) { console.SetCheatsEnabled(value); },
        [](bool) { return true; }));
    m_registrations.push_back(Bind<float>(
        console, "time.scale", 1.0f, "Scaled simulation speed; zero pauses", ConsoleFlags::Cheat,
        [] { return Time::GetTimeScale(); }, [](float value) { Time::SetTimeScale(value); },
        [](float value) { return value >= 0.0f && value <= 100.0f; }));
    m_registrations.push_back(Bind<int>(
        console, "render.max_fps", app.GetMaxFrameRate(), "Frame limit; zero is unlimited", ConsoleFlags::Saved,
        [&app] { return app.GetMaxFrameRate(); }, [&app](int value) { app.SetMaxFrameRate(value); },
        [](int value) { return value >= 0 && value <= 1000; }));
    m_registrations.push_back(Bind<bool>(
        console, "profiler.enabled", true, "Record frame scopes and counters", {}, [] { return Profiler::IsEnabled(); },
        [](bool value) { Profiler::SetEnabled(value); }, [](bool) { return true; }));
}

} // namespace Spot
