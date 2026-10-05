#pragma once

#include "assets/asset_database.h"
#include "assets/asset_manager.h"
#include "audio/audio_device.h"
#include "core/time.h"
#include "events/event.h"
#include "graphics/graphics_device.h"
#include "input/input_state.h"
#include "scene/scene.h"
#include "window/window.h"

#include <concepts>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace Spot {

class LogSink;

struct AssetConfig {
    // Where imported assets are loaded from. Relative paths are relative to the executable's directory.
    std::filesystem::path importDirectory = "imported";
    // When set and the directory exists, its sources are imported into the import directory at startup, as
    // during development; see AssetDatabase. Shipped games leave it empty and ship the import directory. Relative
    // paths are relative to the executable's directory.
    std::filesystem::path sourceDirectory = {};
};

struct ApplicationConfig {
    WindowConfig window = {};
    GraphicsConfig graphics = {};
    AudioConfig audio = {};
    AssetConfig assets = {};
    // When set, the log is also written to this file.
    std::filesystem::path logFile = {};
    // Caps how many frames run per second; 0 means unlimited.
    int maxFrameRate = 0;
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

    bool IsRunning() const { return m_running; }

    // Only available while Run() is running.
    Window& GetWindow();
    // Only available while Run() is running, and only when graphics are enabled.
    GraphicsDevice& GetGraphicsDevice();
    // Only available while Run() is running. Without audio output, because it is disabled or the machine has none,
    // sounds play silently.
    AudioDevice& GetAudioDevice();
    // Only available while Run() is running. Also reachable through the Spot::Assets functions.
    AssetManager& GetAssets();
    // Null unless the configuration has a source directory that exists.
    AssetDatabase* GetAssetDatabase() { return m_assetDatabase.get(); }

    // Scene changes are queued and applied at the start of the next frame, in the order they were asked for, so a
    // scene that asks for a change is never destroyed while it runs. Unloading a scene calls its OnUnload(), then
    // clears and destroys it; loading one calls its OnLoad(). Changes asked for from those hooks wait for the frame
    // after.
    //
    // Each frame, the persistent scene and then the loaded scenes, in load order, update after
    // Application::OnUpdate(), receive events after Application::OnEvent() and render before
    // Application::OnRender(). They are unloaded after OnStop(), while the devices still exist.

    // Replaces every loaded scene with this one, and cancels the changes still queued. A null scene just unloads
    // them all.
    void LoadScene(std::unique_ptr<Scene> scene);
    // Loads the scene next to the ones already loaded.
    void LoadSceneAdditive(std::unique_ptr<Scene> scene);
    // Unloads a loaded scene. A scene still waiting to be loaded is dropped without being loaded.
    void UnloadScene(Scene& scene);

    template <std::derived_from<Scene> T, typename... Args>
    T& LoadScene(Args&&... args) {
        auto scene = std::make_unique<T>(std::forward<Args>(args)...);
        T& loaded = *scene;
        LoadScene(std::move(scene));
        return loaded;
    }

    template <std::derived_from<Scene> T, typename... Args>
    T& LoadSceneAdditive(Args&&... args) {
        auto scene = std::make_unique<T>(std::forward<Args>(args)...);
        T& loaded = *scene;
        LoadSceneAdditive(std::move(scene));
        return loaded;
    }

    // The main scene: the first loaded scene, which is the one LoadScene() loaded unless it was unloaded since.
    // Null when no scene is loaded.
    Scene* GetScene() const { return m_scenes.empty() ? nullptr : m_scenes.front().get(); }
    // Every loaded scene, in load order; the persistent scene is not part of them.
    std::vector<Scene*> GetScenes() const;

    // A scene loaded for the whole of Run(), whatever other scenes come and go. Entities that must survive scene
    // changes, like the player or the music, move into it. Only available while Run() is running.
    Scene& GetPersistentScene();
    // Moves the entity and its descendants to the persistent scene.
    void MakePersistent(Entity& entity);

protected:
    virtual void OnStart() {}
    virtual void OnEvent(const Event&) {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    // Called after OnUpdate() on frames that can be drawn; all GPU commands of the frame are recorded here.
    virtual void OnRender(RenderContext&) {}
    virtual void OnStop() {}

private:
    enum class SceneChangeKind { Replace, Add, Remove };

    struct SceneChange {
        SceneChangeKind kind;
        // The scene to load, for Replace and Add.
        std::unique_ptr<Scene> scene;
        // The scene to unload, for Remove.
        Scene* target = nullptr;
    };

    void HandleEvent(const Event& event);
    void ApplySceneChanges();
    void AddScene(std::unique_ptr<Scene> scene);
    void RemoveScene(const Scene* scene);
    void RemoveAllScenes();

    ApplicationConfig m_config;
    std::shared_ptr<LogSink> m_logFile;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<GraphicsDevice> m_graphics;
    std::unique_ptr<AudioDevice> m_audio;
    std::unique_ptr<AssetDatabase> m_assetDatabase;
    std::unique_ptr<AssetManager> m_assets;
    std::unique_ptr<Scene> m_persistentScene;
    std::vector<std::unique_ptr<Scene>> m_scenes;
    std::vector<SceneChange> m_sceneChanges;
    InputState m_input;
    FrameClock m_clock;
    bool m_running = false;
};

} // namespace Spot
