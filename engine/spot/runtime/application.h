#pragma once

#include "spot/assets/asset_manager.h"
#include "spot/audio/audio_device.h"
#include "spot/core/time/time.h"
#include "spot/graphics/graphics_device.h"
#include "spot/platform/event.h"
#include "spot/platform/input_action.h"
#include "spot/platform/input_state.h"
#include "spot/platform/window.h"
#include "spot/runtime/game_settings.h"
#include "spot/scene/scene.h"

#include <concepts>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace Spot {

class EngineConsole;
class ConsoleInput;
class Font;
class Renderer2D;
class FrameCapture;
struct RuntimeStats;

class LogSink;
class SceneRenderer;

struct AssetConfig {
    // Where imported assets are loaded from, which the tools import them into; when unset, the game's (see
    // GameSettings::importDirectory), or "imported" without a game. Relative paths are relative to the executable's
    // directory.
    std::filesystem::path importDirectory = {};
};

struct ApplicationConfig {
    // The game the application runs, whose startup scene it loads after OnStart() unless OnStart() loaded a scene
    // itself. When unset, it is read from GameSettings::FileName beside the executable, as games built by the tools
    // have it; without that file, the application starts with no scene.
    std::optional<GameSettings> game = std::nullopt;
    WindowConfig window = {};
    GraphicsConfig graphics = {};
    AudioConfig audio = {};
    AssetConfig assets = {};
    // When set, the log is also written to this file. The log of the run before is kept beside it, with ".previous"
    // before its extension, so what led to a crash is still there after starting again. Applications that run a game
    // and set none log to game.log beside the executable.
    std::filesystem::path logFile = {};
    // Where crash reports go; see InstallCrashHandler(), which the application calls. When unset, they go in a
    // "crashes" folder beside the log file, if there is one, and only to the log otherwise.
    std::filesystem::path crashReportDirectory = {};
    // Where the game keeps what it saves for the player, such as their input rebinds. When unset, the platform's
    // user data folder for the game (see GetUserDataDirectory()), "Spot Games/<game name>".
    std::filesystem::path userDataDirectory = {};
    // Tells the user when the application crashed, and where the report is, as tools do.
    bool showCrashMessage = false;
    // Caps how many frames run per second; 0 means unlimited.
    int maxFrameRate = 0;
    // Draws the active scene into the window with a SceneRenderer, before OnRender(), so a
    // game needs no code to show them. Tools that show scenes their own way, such as the editor, turn it off.
    bool renderScenes = true;
    // The Grave key opens an event-driven developer console. Tools with their own console turn it off.
    bool consoleOverlay = true;
};

class Application {
public:
    explicit Application(ApplicationConfig config = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Opens the window and runs frames until Quit() is called or the window is closed.
    // Returns the process exit code, which is a failure when the window cannot be opened.
    int Run();
    void Quit();
    // Ends the game, as game code does, such as from a menu's Quit button. Applications quit; tools that run games
    // inside them, such as the editor, stop the game instead.
    virtual void QuitGame() { Quit(); }

    bool IsRunning() const { return m_running; }
    // Null when the application runs no game; see ApplicationConfig::game.
    const GameSettings* GetGameSettings() const { return m_config.game ? &*m_config.game : nullptr; }
    // Changes the game the application runs, as tools that run games inside them do. Its sections apply to the
    // scenes loaded from then on; what the application set up when it started, such as its window and where its
    // assets are, stays. The action map is emptied and given the game's actions.
    void SetGameSettings(std::optional<GameSettings> game);
    RenderingSettings GetRenderingSettings() const;
    // Applies at the next scene render; input bindings and project defaults are retained.
    void SetRenderingSettings(RenderingSettings rendering);
    std::uint64_t GetRenderingRevision() const { return m_renderingRevision; }

    // Caps how many frames run per second from the next frame on; 0 means unlimited.
    int GetMaxFrameRate() const { return m_config.maxFrameRate; }
    void SetMaxFrameRate(int maxFrameRate);
    bool IsSceneRenderingEnabled() const { return m_config.renderScenes; }
    void SetSceneRenderingEnabled(bool enabled) { m_config.renderScenes = enabled; }
    // A game's imported UI font; keep the asset reference so it stays loaded for the developer overlays.
    void SetDeveloperFont(AssetRef<Font> font) { m_developerFont = std::move(font); }
    ConsoleInput* GetConsoleInput() const { return m_consoleInput.get(); }
    bool IsStatsVisible() const { return m_statsVisible; }
    void SetStatsVisible(bool visible) { m_statsVisible = visible; }
    RuntimeStats GetRuntimeStats() const;
    bool IsProfileStreamEnabled() const { return m_profileStreamEnabled; }
    void SetProfileStreamEnabled(bool enabled) { m_profileStreamEnabled = enabled; }
    // Queues the next complete frame as a PNG in the user's screenshots folder; rejects paths and pending requests.
    bool RequestScreenshot(std::string_view fileName = {});
    // Requests a RenderDoc frame when its optional runtime connected before graphics creation.
    bool RequestFrameCapture(std::string_view fileName = {});

    // Only available while Run() is running.
    Window& GetWindow();
    // Only available while Run() is running, and only when graphics are enabled.
    GraphicsDevice& GetGraphicsDevice();
    // Only available while Run() is running. Without audio output, because it is disabled or the machine has none,
    // sounds play silently.
    AudioDevice& GetAudioDevice();
    // Only available while Run() is running. Also reachable through the Spot::Assets functions.
    AssetManager& GetAssets();
    // The game's actions, which Spot::Input's action queries read; updated after each frame's events.
    InputActionMap& GetInputActions() { return m_actions; }
    // Saves the actions' rebinds (see InputActionMap::Rebind()) in the user data folder, from which the application
    // loads them when it starts. Returns false, after logging why, when they cannot be saved.
    bool SaveInputRebinds();
    // The folder of ApplicationConfig::userDataDirectory, created when first asked for; empty when the application
    // runs no game and none was set.
    std::filesystem::path GetUserDataDirectory();
    static constexpr std::string_view InputRebindsFileName = "input_rebinds.json";
    // The input the application's events build, even while Spot::Input reads another state, as tools running games
    // make it do.
    const InputState& GetInputState() const { return m_input; }

    // Replaces the active scene at the start of the next frame; later requests replace earlier queued ones.
    // OnUnload() runs before retained entities move into the next scene, followed by OnLoad(). Requests from
    // either hook wait for the following frame. Null unloads the scene and destroys all its entities.
    void LoadScene(std::unique_ptr<Scene> scene);

    template <std::derived_from<Scene> T, typename... Args>
    T& LoadScene(Args&&... args) {
        auto scene = std::make_unique<T>(std::forward<Args>(args)...);
        T& loaded = *scene;
        LoadScene(std::move(scene));
        return loaded;
    }

    // The active scene, or null when none is loaded.
    Scene* GetScene() const { return m_scene.get(); }

    // Pauses simulation and sounds; events, rendering and chunk completion continue.
    // StepScenes() advances one frame while paused.
    void SetScenesPaused(bool paused);
    bool AreScenesPaused() const { return m_scenesPaused; }
    void StepScenes() { m_stepScenes = true; }

protected:
    virtual void OnStart() {}
    virtual void OnEvent(const Event&) {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    // Called after OnUpdate() on frames that can be drawn; all GPU commands of the frame are recorded here.
    virtual void OnRender(RenderContext&) {}
    virtual void OnStop() {}
    // Called after a WindowCloseEvent has reached OnEvent() and the scenes. Returning false keeps the application
    // running, such as to ask about unsaved changes first.
    virtual bool OnCloseRequested() { return true; }
    // Whether the scenes receive the event, after OnEvent(). Tools that run scenes in a view of their own keep the
    // events meant for the rest of their window from them.
    virtual bool DeliversToScenes(const Event& /*event*/) { return true; }

    // Sends the event as the platform's are sent: to the input state, OnEvent() and the scenes; and closes the
    // application on a WindowCloseEvent that OnCloseRequested() allows.
    void DispatchEvent(const Event& event);

private:
    void UpdateUiTextInput();
    bool m_uiTextInput = false;
    std::optional<RenderingSettings> m_runtimeRendering;
    std::uint64_t m_renderingRevision = 1;
    bool m_renderingDirty = false;
    std::unique_ptr<EngineConsole> m_engineConsole;
    void ApplySceneChanges();
    void RemoveScene();
    void RenderScenes(RenderContext& context);
    void RenderDeveloperOverlay(RenderContext& context);
    void StreamProfileFrame();
    bool PrepareScreenshot(RenderContext& context);
    std::filesystem::path GetCaptureDirectory();
    // Returns false, after logging why, when the game's startup scene cannot be loaded.
    bool LoadStartupScene();

    ApplicationConfig m_config;
    std::shared_ptr<LogSink> m_logFile;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<GraphicsDevice> m_graphics;
    std::unique_ptr<FrameCapture> m_frameCapture;
    std::unique_ptr<Texture> m_screenshotTexture;
    std::filesystem::path m_screenshotPath;
    std::unique_ptr<AudioDevice> m_audio;
    std::unique_ptr<AssetManager> m_assets;
    // Created when there is first something to draw, so applications that draw no scene never pay for it.
    std::unique_ptr<SceneRenderer> m_sceneRenderer;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Scene> m_pendingScene;
    bool m_sceneChangePending = false;
    InputState m_input;
    std::unique_ptr<ConsoleInput> m_consoleInput;
    AssetRef<Font> m_developerFont;
    std::unique_ptr<Renderer2D> m_developerRenderer;
    InputActionMap m_actions;
    std::filesystem::path m_userDataDirectory;
    FrameClock m_clock;
    FrameLimiter m_limiter;
    bool m_running = false;
    bool m_failed = false;
    // The cursor lock last applied to the window, which follows the input state's when it changes.
    bool m_cursorLocked = false;
    bool m_scenesPaused = false;
    bool m_stepScenes = false;
    bool m_statsVisible = false;
    bool m_profileStreamEnabled = false;
    std::optional<std::uint64_t> m_lastStreamedFrame;
};

} // namespace Spot
