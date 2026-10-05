#pragma once

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

namespace Spot {

class LogSink;

struct ApplicationConfig {
    WindowConfig window = {};
    GraphicsConfig graphics = {};
    AudioConfig audio = {};
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

    // Replaces the current scene at the start of the next frame, so the scene that asks for the change is never
    // destroyed while it runs. The old scene gets OnUnload() and is cleared and destroyed, then the new one gets
    // OnLoad(). Loading again before then replaces the pending scene, and a null scene unloads the current one.
    // Each frame, the current scene updates after Application::OnUpdate() and renders before Application::OnRender();
    // it is unloaded after OnStop(), while the devices still exist.
    void LoadScene(std::unique_ptr<Scene> scene);

    template <std::derived_from<Scene> T, typename... Args>
    T& LoadScene(Args&&... args) {
        auto scene = std::make_unique<T>(std::forward<Args>(args)...);
        T& loaded = *scene;
        LoadScene(std::move(scene));
        return loaded;
    }

    // Null until a loaded scene takes over.
    Scene* GetScene() const { return m_scene.get(); }

protected:
    virtual void OnStart() {}
    virtual void OnEvent(const Event&) {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    // Called after OnUpdate() on frames that can be drawn; all GPU commands of the frame are recorded here.
    virtual void OnRender(RenderContext&) {}
    virtual void OnStop() {}

private:
    void HandleEvent(const Event& event);
    void SwitchScene();
    void UnloadScene();

    ApplicationConfig m_config;
    std::shared_ptr<LogSink> m_logFile;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<GraphicsDevice> m_graphics;
    std::unique_ptr<AudioDevice> m_audio;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Scene> m_pendingScene;
    bool m_sceneChangePending = false;
    InputState m_input;
    FrameClock m_clock;
    bool m_running = false;
};

} // namespace Spot
