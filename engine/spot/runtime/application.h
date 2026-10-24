#pragma once

#include "spot/assets/asset_manager.h"
#include "spot/core/time/time.h"
#include "spot/graphics/graphics_device.h"
#include "spot/platform/event.h"
#include "spot/platform/input_action.h"
#include "spot/platform/input_state.h"
#include "spot/platform/window.h"
#include "spot/runtime/game_settings.h"
#include "spot/runtime/module.h"
#include "spot/scene/scene.h"

#include <concepts>
#include <filesystem>
#include <memory>
#include <typeindex>
#include <utility>
#include <vector>

namespace Spot {

class EngineConsole;

class LogSink;

struct AssetConfig {
    // Where imported assets are loaded from, which the tools import them into; when unset, the game's (see
    // GameSettings::importDirectory), or "imported" without a game. Relative paths are relative to the executable's
    // directory.
    std::filesystem::path importDirectory = {};
};

struct ApplicationConfig {
    // The game the application runs: the only way game settings reach it. Run() applies its input actions, the
    // player's rebinds and its sections, and loads its start scene after OnStart() unless OnStart() loaded a scene
    // itself. Games built by the tools read it in main() with GameSettings::LoadBesideExecutable(); without one,
    // the application starts with no scene.
    std::optional<GameSettings> game = std::nullopt;
    WindowConfig window = {};
    GraphicsConfig graphics = {};
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
};

class Application {
public:
    explicit Application(ApplicationConfig config = {});
    virtual ~Application();

    // The application that is running, as game code reaches it. Only available while Run() is running.
    static Application& Get();
    // Null while no application runs.
    static Application* TryGet() { return s_running; }

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

    // Adds a module, made from the arguments, which Run() starts with the others; see Module. Modules are added
    // before Run(), one of each type.
    template <std::derived_from<Module> T, typename... Args>
    T& AddModule(Args&&... args) {
        auto module = std::make_unique<T>(std::forward<Args>(args)...);
        T& added = *module;
        AddModule(typeid(T), std::move(module));
        return added;
    }
    // Adds a module made elsewhere, such as the game's (see CreateGameModule()), under its dynamic type.
    Module& AddModule(std::unique_ptr<Module> module);
    // The module of that exact type, or null when none was added.
    template <std::derived_from<Module> T>
    T* GetModule() const {
        return static_cast<T*>(FindModule(typeid(T)));
    }
    // Null when the application runs no game; see ApplicationConfig::game.
    const GameSettings* GetGameSettings() const { return m_config.game ? &*m_config.game : nullptr; }
    RenderingSettings GetRenderingSettings() const;
    // Applies at the next scene render; input bindings and project defaults are retained.
    void SetRenderingSettings(RenderingSettings rendering);
    // Changes whenever the rendering settings do, so what was made from them can be made again.
    std::uint64_t GetRenderingRevision() const { return m_renderingRevision; }
    // Changes whenever SetGameSettings() is called, so modules can apply the game's sections again.
    std::uint64_t GetGameSettingsRevision() const { return m_gameSettingsRevision; }

    // Caps how many frames run per second from the next frame on; 0 means unlimited.
    int GetMaxFrameRate() const { return m_config.maxFrameRate; }
    void SetMaxFrameRate(int maxFrameRate);

    // Only available while Run() is running.
    Window& GetWindow();
    // Only available while Run() is running, and only when graphics are enabled.
    GraphicsDevice& GetGraphicsDevice();
    bool HasGraphicsDevice() const { return m_graphics != nullptr; }
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
    InputState& GetInputState() { return m_input; }

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

    // Whether the scene receives the event, after OnEvent(); modules routing events to the scene ask too. Tools that
    // run scenes in a view of their own keep the events meant for the rest of their window from them.
    virtual bool DeliversToScenes(const Event& /*event*/) { return true; }

    // The active scene, or null when none is loaded.
    Scene* GetScene() const { return m_scene.get(); }

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

    // Changes the game the application runs, as tools that run games inside them do, from their own subclass. Its
    // sections apply to the scenes loaded from then on; what the application set up when it started, such as its
    // window and where its assets are, stays. The action map is emptied and given the game's actions.
    void SetGameSettings(std::optional<GameSettings> game);

    // Sends the event as the platform's are sent: to the input state, OnEvent() and the scenes; and closes the
    // application on a WindowCloseEvent that OnCloseRequested() allows.
    void DispatchEvent(const Event& event);

private:
    void AddModule(std::type_index type, std::unique_ptr<Module> module);
    Module* FindModule(std::type_index type) const;
    std::optional<RenderingSettings> m_runtimeRendering;
    std::uint64_t m_renderingRevision = 1;
    std::uint64_t m_gameSettingsRevision = 1;
    std::unique_ptr<EngineConsole> m_engineConsole;
    void StartLog();
    void StopLog();
    WindowConfig GetWindowConfig() const;
    bool StartWindow();
    bool StartGraphics();
    void StartAssets();
    void StartInput();
    void StartModules();
    void RunFrame();
    void Stop();
    void ApplySceneChanges();
    void RemoveScene();
    // Returns false, after logging why, when the game's startup scene cannot be loaded.
    bool LoadStartupScene();

    static inline Application* s_running = nullptr;

    ApplicationConfig m_config;
    std::vector<std::pair<std::type_index, std::unique_ptr<Module>>> m_modules;
    std::shared_ptr<LogSink> m_logFile;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<GraphicsDevice> m_graphics;
    std::unique_ptr<AssetManager> m_assets;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Scene> m_pendingScene;
    bool m_sceneChangePending = false;
    InputState m_input;
    InputActionMap m_actions;
    std::filesystem::path m_userDataDirectory;
    FrameClock m_clock;
    FrameLimiter m_limiter;
    bool m_running = false;
    bool m_failed = false;
    // The cursor lock last applied to the window, which follows the input state's when it changes.
    bool m_cursorLocked = false;
};

} // namespace Spot
