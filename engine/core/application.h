#pragma once

#include "events/event.h"
#include "input/input_state.h"
#include "window/window.h"

#include <filesystem>
#include <memory>

namespace Spot {

class LogSink;

struct ApplicationConfig {
    WindowConfig window;
    // When set, the log is also written to this file.
    std::filesystem::path logFile = {};
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

protected:
    virtual void OnStart() {}
    virtual void OnEvent(const Event&) {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    virtual void OnStop() {}

private:
    void HandleEvent(const Event& event);

    ApplicationConfig m_config;
    std::shared_ptr<LogSink> m_logFile;
    std::unique_ptr<Window> m_window;
    InputState m_input;
    bool m_running = false;
};

} // namespace Spot
