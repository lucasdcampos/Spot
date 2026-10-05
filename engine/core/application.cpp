#include "core/application.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/log_sinks.h"
#include "core/paths.h"
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
            Log::GetEngineLogger().Warning("Could not open log file '{}'", PathToUtf8(m_config.logFile));
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

    if (m_config.graphics.enabled) {
        m_graphics = GraphicsDevice::Create(*m_window, m_config.graphics);
        if (!m_graphics) {
            m_window.reset();
            return EXIT_FAILURE;
        }
    }

    Input::SetState(&m_input);
    Time::SetClock(&m_clock);
    FrameLimiter limiter(m_config.maxFrameRate);
    m_clock.Start(std::chrono::steady_clock::now());
    m_running = true;
    OnStart();

    while (m_running) {
        Platform::SleepFor(limiter.GetWaitTime(std::chrono::steady_clock::now()));
        m_clock.Tick(std::chrono::steady_clock::now());

        m_input.BeginFrame();
        Platform::PollEvents([this](const Event& event) { HandleEvent(event); });
        if (!m_running) {
            break;
        }

        OnUpdate(m_clock.GetDeltaTime());

        if (m_graphics) {
            if (RenderContext* context = m_graphics->BeginFrame()) {
                OnRender(*context);
                m_graphics->EndFrame();
            }
        }
    }

    OnStop();
    Time::SetClock(nullptr);
    Input::SetState(nullptr);
    m_graphics.reset();
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

GraphicsDevice& Application::GetGraphicsDevice() {
    SPOT_ASSERT(m_graphics, "The graphics device only exists while Run() is running with graphics enabled");
    return *m_graphics;
}

void Application::HandleEvent(const Event& event) {
    m_input.OnEvent(event);
    OnEvent(event);

    if (event.Is<WindowCloseEvent>()) {
        Quit();
    }
}

} // namespace Spot
