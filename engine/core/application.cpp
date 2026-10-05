#include "core/application.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/log_sinks.h"
#include "input/input.h"
#include "platform/platform.h"

#include <chrono>
#include <cstdlib>
#include <utility>

namespace Spot {

Application::Application(ApplicationConfig config) : m_config(std::move(config)) {
    if (!m_config.logFile.empty()) {
        auto file = std::make_shared<FileSink>(m_config.logFile);
        if (file->IsOpen()) {
            m_logFile = std::move(file);
            Log::AddSink(m_logFile);
        } else {
            Log::GetEngineLogger().Warning("Could not open log file '{}'", m_config.logFile.string());
        }
    }
}

Application::~Application() {
    if (m_logFile) {
        Log::RemoveSink(m_logFile);
    }
}

int Application::Run() {
    SPOT_ASSERT(!m_window, "Run() is already running");

    m_window = Window::Create(m_config.window);
    if (!m_window) {
        return EXIT_FAILURE;
    }

    Input::SetState(&m_input);
    m_running = true;
    OnStart();

    auto previousFrame = std::chrono::steady_clock::now();
    while (m_running) {
        m_input.BeginFrame();
        Platform::PollEvents([this](const Event& event) { HandleEvent(event); });
        if (!m_running) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto deltaTime = std::chrono::duration<float>(now - previousFrame).count();
        previousFrame = now;

        OnUpdate(deltaTime);
    }

    OnStop();
    Input::SetState(nullptr);
    m_window.reset();
    return EXIT_SUCCESS;
}

void Application::Quit() {
    m_running = false;
}

Window& Application::GetWindow() {
    SPOT_ASSERT(m_window, "The window only exists while Run() is running");
    return *m_window;
}

void Application::HandleEvent(const Event& event) {
    m_input.OnEvent(event);
    OnEvent(event);

    if (event.Is<WindowCloseEvent>()) {
        Quit();
    }
}

} // namespace Spot
