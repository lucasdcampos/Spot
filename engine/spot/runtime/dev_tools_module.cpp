#include "spot/runtime/dev_tools_module.h"

#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"
#include "spot/core/io/paths.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/screenshot.h"
#include "spot/platform/frame_capture.h"
#include "spot/platform/process.h"
#include "spot/render/font.h"
#include "spot/render/renderer_2d.h"
#include "spot/runtime/application.h"
#include "spot/runtime/console_input.h"
#include "spot/runtime/console_overlay.h"
#include "spot/runtime/profile_stream.h"
#include "spot/runtime/runtime_stats.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <utility>

namespace Spot {

namespace {

bool IsCaptureName(std::string_view name) {
    return !name.empty() && name.size() <= 128 && name != "." && name != ".." && name.back() != ' ' &&
           name.back() != '.' && std::ranges::none_of(name, [](unsigned char c) {
               return c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos;
           });
}

std::string CaptureName(std::string_view prefix, std::string_view extension) {
    const auto stamp =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    return std::format("{}-{}{}", prefix, stamp, extension);
}

ConsoleRegistration BindBool(Console& console, std::string name, std::string description, ConsoleFlags flags,
                             bool& value) {
    return console.RegisterVariable(
        std::move(name), std::move(description), flags, FormatConsoleValue(value),
        [&value] { return FormatConsoleValue(value); },
        [&value](std::string_view text) { return ParseConsoleValue(text, value); });
}

} // namespace

DevToolsModule::DevToolsModule(DevToolsConfig config) : m_config(config) {
    if (m_config.frameCapture) {
        m_frameCapture = FrameCapture::Connect();
    }
    m_profileStreamEnabled = Process::GetEnvironmentValue("SPOT_PROFILE_STREAM") == "1";
}

DevToolsModule::~DevToolsModule() = default;

void DevToolsModule::OnStart() {
    Application& application = GetApplication();
    Console& console = Console::Get();
    if (m_config.consoleOverlay) {
        m_consoleInput = std::make_unique<ConsoleInput>(console, application.GetInputState());
    }
    m_console.push_back(BindBool(console, "stats", "Show frame, rendering, physics, audio and memory statistics",
                                 ConsoleFlags::Saved, m_statsVisible));
    m_console.push_back(BindBool(console, "profiler.stream", "Send completed frames over the game log pipe", {},
                                 m_profileStreamEnabled));
    m_console.push_back(console.Register(
        {"screenshot",
         "Save the next complete frame as PNG",
         {},
         [this](std::span<const std::string> arguments) -> ConsoleResult {
             if (arguments.size() > 1) {
                 return {false, "Usage: screenshot [name.png]"};
             }
             if (!RequestScreenshot(arguments.empty() ? std::string_view{} : arguments[0])) {
                 return {false, "Screenshot requires running graphics, a bare PNG name and no pending screenshot"};
             }
             return {true, "Screenshot queued in the user screenshots folder"};
         }}));
    m_console.push_back(console.Register(
        {"capture",
         "Capture the next frame with optional RenderDoc",
         {},
         [this](std::span<const std::string> arguments) -> ConsoleResult {
             if (arguments.size() > 1) {
                 return {false, "Usage: capture [name]"};
             }
             if (!RequestFrameCapture(arguments.empty() ? std::string_view{} : arguments[0])) {
                 return {false,
                         "Frame capture unavailable: connect RenderDoc before graphics and use a bare file name"};
             }
             return {true, "RenderDoc frame requested in the user screenshots folder"};
         }}));
}

void DevToolsModule::OnStop() {
    // The application ended the last profiled frame before stopping its modules.
    StreamProfileFrame();
    SaveScreenshot();
    m_console.clear();
    if (m_consoleInput && m_consoleInput->IsOpen()) {
        GetApplication().GetWindow().StopTextInput();
    }
    m_consoleInput.reset();
    m_renderer.reset();
    m_font = {};
    m_screenshotTexture.reset();
    m_screenshotPath.clear();
    m_lastStreamedFrame.reset();
}

bool DevToolsModule::OnEvent(const Event& event) {
    if (!m_consoleInput) {
        return false;
    }
    const bool wasOpen = m_consoleInput->IsOpen();
    const bool consumed = m_consoleInput->OnEvent(event);
    if (wasOpen != m_consoleInput->IsOpen()) {
        Window& window = GetApplication().GetWindow();
        if (m_consoleInput->IsOpen()) {
            window.StartTextInput();
        } else {
            window.StopTextInput();
        }
    }
    return consumed;
}

void DevToolsModule::OnUpdate(float /*deltaTime*/) {
    StreamProfileFrame();
    SaveScreenshot();
}

std::filesystem::path DevToolsModule::GetCaptureDirectory() {
    auto directory = GetApplication().GetUserDataDirectory();
    if (directory.empty()) {
        directory = Spot::GetUserDataDirectory("Spot", "Developer");
    }
    return directory.empty() ? directory : directory / "screenshots";
}

bool DevToolsModule::RequestScreenshot(std::string_view fileName) {
    const std::string name = fileName.empty() ? CaptureName("screenshot", ".png") : std::string(fileName);
    if (!IsCaptureName(name) || !name.ends_with(".png") || !GetApplication().HasGraphicsDevice() ||
        !m_screenshotPath.empty()) {
        return false;
    }
    const auto directory = GetCaptureDirectory();
    if (directory.empty()) {
        return false;
    }
    m_screenshotPath = directory / Utf8ToPath(name);
    return true;
}

bool DevToolsModule::RequestFrameCapture(std::string_view fileName) {
    const std::string name = fileName.empty() ? CaptureName("frame", "") : std::string(fileName);
    if (!IsCaptureName(name) || !GetApplication().HasGraphicsDevice() || !m_frameCapture) {
        return false;
    }
    const auto directory = GetCaptureDirectory();
    return !directory.empty() && m_frameCapture->Request(directory / Utf8ToPath(name));
}

void DevToolsModule::OnBeginRender(RenderContext& context) {
    if (m_screenshotPath.empty() || m_screenshotTexture) {
        return;
    }
    GraphicsDevice& device = GetApplication().GetGraphicsDevice();
    m_screenshotTexture = device.CreateTexture({.width = static_cast<std::uint32_t>(context.GetWidth()),
                                                .height = static_cast<std::uint32_t>(context.GetHeight()),
                                                .format = device.GetWindowFormat(),
                                                .renderTarget = true});
    if (!m_screenshotTexture || !context.SetFrameTarget(m_screenshotTexture.get())) {
        Log::GetEngineLogger().Warning("Could not create the screenshot target");
        m_screenshotTexture.reset();
        m_screenshotPath.clear();
        return;
    }
    auto clear = context.BeginRenderPass({.clearColor = Color::Black});
}

void DevToolsModule::OnEndRender(RenderContext& context) {
    RenderOverlay(context);
    if (m_screenshotTexture && !m_screenshotDrawn) {
        context.SetFrameTarget(nullptr);
        context.BlitTextureToFrame(*m_screenshotTexture);
        m_screenshotDrawn = true;
    }
}

void DevToolsModule::SaveScreenshot() {
    if (!m_screenshotDrawn) {
        return;
    }
    const bool saved =
        Spot::SaveScreenshot(GetApplication().GetGraphicsDevice(), *m_screenshotTexture, m_screenshotPath);
    Log::GetEngineLogger().Write(saved ? LogLevel::Info : LogLevel::Warning, "Screenshot {}: {}",
                                 saved ? "saved" : "failed", PathToUtf8(m_screenshotPath));
    m_screenshotPath.clear();
    m_screenshotTexture.reset();
    m_screenshotDrawn = false;
}

void DevToolsModule::StreamProfileFrame() {
    if (!m_profileStreamEnabled || !Profiler::IsEnabled()) {
        return;
    }
    const auto frames = Profiler::GetFrames();
    if (!frames.empty() && frames.back()->number != m_lastStreamedFrame) {
        m_lastStreamedFrame = frames.back()->number;
        Log::GetEngineLogger().Info("{}", EncodeProfileFrame(*frames.back()));
    }
}

void DevToolsModule::RenderOverlay(RenderContext& context) {
    const bool consoleOpen = m_consoleInput && m_consoleInput->IsOpen();
    if ((!consoleOpen && !m_statsVisible) || !m_font) {
        return;
    }
    if (!m_renderer) {
        m_renderer = std::make_unique<Renderer2D>(GetApplication().GetGraphicsDevice());
    }
    if (!m_renderer->IsValid()) {
        return;
    }
    const Vector2 size{static_cast<float>(context.GetWidth()), static_cast<float>(context.GetHeight())};
    auto pass = context.BeginRenderPass({});
    m_renderer->Begin(pass, Camera2D{.position = size * 0.5f}, size);
    if (m_statsVisible) {
        DrawStatsOverlay(RuntimeStats::Sample(), *m_font, *m_renderer, size);
    }
    if (consoleOpen) {
        DrawConsoleOverlay(Console::Get(), *m_consoleInput, *m_font, *m_renderer, size);
    }
    m_renderer->End();
}

} // namespace Spot
