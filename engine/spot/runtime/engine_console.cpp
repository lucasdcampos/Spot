#include "spot/runtime/engine_console.h"

#include "spot/audio/audio_device.h"
#include "spot/audio/playback.h"
#include "spot/core/debug/profiler.h"
#include "spot/core/time/time.h"
#include "spot/runtime/application.h"
#include "spot/scene/audio/audio_system.h"
#include "spot/scene/physics/physics_world.h"

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
    m_registrations.push_back(Bind<float>(
        console, "physics.gravity", -9.81f, "Main scene gravity on the Y axis", ConsoleFlags::Cheat,
        [&app] { return app.GetScene() ? app.GetScene()->GetPhysics().GetGravity().y : -9.81f; },
        [&app](float value) {
            if (auto* scene = app.GetScene()) {
                auto gravity = scene->GetPhysics().GetGravity();
                gravity.y = value;
                scene->GetPhysics().SetGravity(gravity);
            }
        },
        [](float value) { return std::abs(value) <= 1000.0f; }));
    m_registrations.push_back(Bind<bool>(
        console, "physics.debug", false, "Draw collision shapes in the main scene", {},
        [&app] { return app.GetScene() && app.GetScene()->GetPhysics().GetDebugDraw() != PhysicsDebugFlags::None; },
        [&app](bool value) {
            if (auto* scene = app.GetScene()) {
                scene->GetPhysics().SetDebugDraw(value ? PhysicsDebugFlags::Colliders : PhysicsDebugFlags::None);
            }
        },
        [](bool) { return true; }));
    m_registrations.push_back(Bind<bool>(
        console, "render.enabled", app.IsSceneRenderingEnabled(), "Render the application's scenes", {},
        [&app] { return app.IsSceneRenderingEnabled(); }, [&app](bool value) { app.SetSceneRenderingEnabled(value); },
        [](bool) { return true; }));
    m_registrations.push_back(Bind<int>(
        console, "render.max_fps", app.GetMaxFrameRate(), "Frame limit; zero is unlimited", ConsoleFlags::Saved,
        [&app] { return app.GetMaxFrameRate(); }, [&app](int value) { app.SetMaxFrameRate(value); },
        [](int value) { return value >= 0 && value <= 1000; }));
    m_registrations.push_back(Bind<float>(
        console, "audio.volume", Audio::GetDevice() ? Audio::GetDevice()->GetMasterVolume() : 1.0f, "Master audio gain",
        ConsoleFlags::Saved, [] { return Audio::GetDevice() ? Audio::GetDevice()->GetMasterVolume() : 1.0f; },
        [](float value) {
            if (auto* audio = Audio::GetDevice()) {
                audio->SetMasterVolume(value);
            }
        },
        [](float value) { return value >= 0.0f && value <= 4.0f; }));
    m_registrations.push_back(Bind<bool>(
        console, "profiler.enabled", true, "Record frame scopes and counters", {}, [] { return Profiler::IsEnabled(); },
        [](bool value) { Profiler::SetEnabled(value); }, [](bool) { return true; }));
    m_registrations.push_back(Bind<bool>(
        console, "profiler.stream", app.IsProfileStreamEnabled(), "Send completed frames over the game log pipe", {},
        [&app] { return app.IsProfileStreamEnabled(); }, [&app](bool value) { app.SetProfileStreamEnabled(value); },
        [](bool) { return true; }));
    m_registrations.push_back(Bind<bool>(
        console, "stats", false, "Show frame, rendering, physics, audio and memory statistics", ConsoleFlags::Saved,
        [&app] { return app.IsStatsVisible(); }, [&app](bool value) { app.SetStatsVisible(value); },
        [](bool) { return true; }));
    m_registrations.push_back(console.Register(
        {"screenshot",
         "Save the next complete frame as PNG",
         {},
         [&app](std::span<const std::string> arguments) -> ConsoleResult {
             if (arguments.size() > 1) {
                 return {false, "Usage: screenshot [name.png]"};
             }
             if (!app.RequestScreenshot(arguments.empty() ? std::string_view{} : arguments[0])) {
                 return {false, "Screenshot requires running graphics, a bare PNG name and no pending screenshot"};
             }
             return {true, "Screenshot queued in the user screenshots folder"};
         }}));
    m_registrations.push_back(console.Register(
        {"capture",
         "Capture the next frame with optional RenderDoc",
         {},
         [&app](std::span<const std::string> arguments) -> ConsoleResult {
             if (arguments.size() > 1) {
                 return {false, "Usage: capture [name]"};
             }
             if (!app.RequestFrameCapture(arguments.empty() ? std::string_view{} : arguments[0])) {
                 return {false,
                         "Frame capture unavailable: connect RenderDoc before graphics and use a bare file name"};
             }
             return {true, "RenderDoc frame requested in the user screenshots folder"};
         }}));
}

} // namespace Spot
