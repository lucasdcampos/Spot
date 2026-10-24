#include "spot/runtime/application.h"
#include "spot/scene/scene_assets.h"

#include "spot/assets/assets.h"
#include "spot/core/debug/assert.h"
#include "spot/core/debug/log.h"
#include "spot/core/debug/log_sinks.h"
#include "spot/core/debug/profiler.h"
#include "spot/core/io/paths.h"
#include "spot/platform/crash_handler.h"
#include "spot/platform/input.h"
#include "spot/platform/platform.h"
#include "spot/platform/process.h"
#include "spot/render/debug_draw.h"
#include "spot/runtime/engine_console.h"
#include "spot/runtime/save_data.h"
#include "spot/scene/scene_assets.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <ranges>
#include <system_error>
#include <utility>

namespace Spot {

Application::Application(ApplicationConfig config) : m_config(std::move(config)) {
}

Application::~Application() = default;

// Startup, in order: what tells of a crash, the window, the graphics device, the assets, the input, the modules,
// OnStart() and the start scene. Each step needs only the ones before it.
int Application::Run() {
    SPOT_ASSERT(!m_window, "Run() is already running");
    m_failed = false;
    StartLog();
    if (!StartWindow() || !StartGraphics()) {
        m_graphics.reset();
        m_window.reset();
        StopLog();
        return EXIT_FAILURE;
    }
    StartAssets();
    StartInput();
    m_running = true;
    StartModules();
    OnStart();
    if (m_running && !m_scene && !m_sceneChangePending && !LoadStartupScene()) {
        m_failed = true;
        Quit();
    }

    while (m_running) {
        RunFrame();
    }

    Stop();
    return m_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

void Application::StartLog() {
    // A game that crashes where it was shipped leaves what led to it.
    std::filesystem::path logFile = m_config.logFile;
    if (m_config.game && logFile.empty()) {
        logFile = GetExecutableDirectory() / "game.log";
    }
    if (!logFile.empty()) {
        std::error_code error;
        if (std::filesystem::exists(logFile, error)) {
            std::filesystem::path previous = logFile;
            previous.replace_extension(".previous" + PathToUtf8(logFile.extension()));
            std::filesystem::rename(logFile, previous, error);
        }
        auto file = std::make_shared<FileSink>(logFile);
        if (file->IsOpen()) {
            m_logFile = std::move(file);
            Log::AddSink(m_logFile);
        } else {
            Log::GetEngineLogger().Warning("Could not open log file '{}'", PathToUtf8(logFile));
        }
    }
    std::filesystem::path reports = m_config.crashReportDirectory;
    if (reports.empty() && m_logFile) {
        reports = std::filesystem::absolute(logFile).parent_path() / "crashes";
    }
    InstallCrashHandler({.applicationName = GetWindowConfig().title,
                         .reportDirectory = reports,
                         .showMessage = m_config.showCrashMessage});
}

void Application::StopLog() {
    if (m_logFile) {
        Log::RemoveSink(m_logFile);
        m_logFile.reset();
    }
}

WindowConfig Application::GetWindowConfig() const {
    WindowConfig window = m_config.window;
    // Only the default title gives way to the game's name, so applications can still name their window.
    if (m_config.game && !m_config.game->name.empty() && window.title == WindowConfig{}.title) {
        window.title = m_config.game->name;
    }
    return window;
}

bool Application::StartWindow() {
    m_window = Window::Create(GetWindowConfig());
    return m_window != nullptr;
}

bool Application::StartGraphics() {
    if (!m_config.graphics.enabled) {
        return true;
    }
    m_graphics = GraphicsDevice::Create(*m_window, m_config.graphics);
    return m_graphics != nullptr;
}

void Application::StartAssets() {
    std::filesystem::path importDirectory = m_config.assets.importDirectory;
    if (importDirectory.empty()) {
        importDirectory = Utf8ToPath(m_config.game ? m_config.game->importDirectory : "imported");
    }
    m_assets = std::make_unique<AssetManager>(GetExecutableDirectory() / importDirectory);
    RegisterSceneAssets(*m_assets);
    Detail::SetAssetManager(m_assets.get());
}

void Application::StartInput() {
    Input::SetState(&m_input);
    Input::SetActionMap(&m_actions);
    Time::SetClock(&m_clock);
    m_limiter = FrameLimiter(m_config.maxFrameRate);
    m_clock.Start(std::chrono::steady_clock::now());
    if (m_config.game) {
        m_config.game->sections.input.Apply(m_actions);
    }
    m_actions.SetDefaults();
    // Only games with actions have rebinds, so other applications never make a user data folder.
    if (m_config.game && !m_actions.GetActions().empty()) {
        if (const std::filesystem::path directory = GetUserDataDirectory(); !directory.empty()) {
            m_actions.LoadRebinds(directory / InputRebindsFileName);
        }
    }
}

void Application::StartModules() {
    m_engineConsole = std::make_unique<EngineConsole>(Console::Get(), *this);
    for (auto& [type, module] : m_modules) {
        module->OnStart();
    }
    // Once the modules registered their console settings.
    if (const auto directory = GetUserDataDirectory(); !directory.empty()) {
        const auto result = SaveData(directory).Load(
            "console.json", 1, [](Archive& archive, std::uint32_t) { Console::Get().Serialize(archive); });
        if (!result && result.status != SaveDataStatus::NotFound) {
            Log::GetEngineLogger().Warning("Could not load console settings: {}", result.message);
        }
    }
}

void Application::RunFrame() {
    // Ends the last frame's profile, so the one profiled spans the whole frame, waiting included.
    Profiler::BeginFrame();
    {
        SPOT_PROFILE_SCOPE("Application::Wait");
        Platform::SleepFor(m_limiter.GetWaitTime(std::chrono::steady_clock::now()));
    }
    m_clock.Tick(std::chrono::steady_clock::now());
    // Assets loading in the background whose files are read become ready for this frame.
    m_assets->Update();

    {
        SPOT_PROFILE_SCOPE("Application::Events");
        m_input.BeginFrame();
        Platform::PollEvents([this](const Event& event) { DispatchEvent(event); });
        // From whatever state is current, so games played in the editor read the editor's.
        Input::UpdateActions();
    }
    if (!m_running) {
        return;
    }

    {
        SPOT_PROFILE_SCOPE("Application::LoadScenes");
        ApplySceneChanges();
        if (m_scene)
            m_scene->FinishChunkLoads();
    }
    {
        SPOT_PROFILE_SCOPE("Application::OnUpdate");
        OnUpdate(m_clock.GetDeltaTime());
    }
    if (!m_scenesPaused || std::exchange(m_stepScenes, false)) {
        if (m_scene)
            m_scene->Update(m_clock.GetDeltaTime());
    }
    for (auto& [type, module] : m_modules) {
        module->OnUpdate(m_clock.GetDeltaTime());
    }
    if (Profiler::IsEnabled()) {
        Profiler::Count("Resident memory", static_cast<double>(Process::GetResidentMemoryBytes()));
    }
    // Only changes are applied, so applications can still capture the mouse themselves.
    if (m_input.IsCursorLockApplied() != m_cursorLocked) {
        m_cursorLocked = m_input.IsCursorLockApplied();
        m_window->SetMouseCaptured(m_cursorLocked);
    }

    if (m_graphics) {
        SPOT_PROFILE_SCOPE("Application::Render");
        if (RenderContext* context = m_graphics->BeginFrame()) {
            context->BeginGpuTimer("Frame");
            for (auto& [type, module] : m_modules) {
                module->OnBeginRender(*context);
            }
            if (m_scene) {
                m_scene->PrepareToRender();
                m_scene->OnRender(*context);
            }
            for (auto& [type, module] : m_modules) {
                module->OnRender(*context);
            }
            {
                SPOT_PROFILE_SCOPE("Application::OnRender");
                OnRender(*context);
            }
            for (auto& module : std::views::reverse(m_modules)) {
                module.second->OnEndRender(*context);
            }
            context->EndGpuTimer();
            {
                SPOT_PROFILE_SCOPE("GraphicsDevice::EndFrame");
                m_graphics->EndFrame();
            }
            // The GPU times of the latest frame the GPU finished, as counters in milliseconds.
            if (Profiler::IsEnabled()) {
                for (const GpuTiming& timing : m_graphics->GetGpuTimings()) {
                    Profiler::Count(Profiler::Intern("GPU " + timing.name + " (ms)"), timing.milliseconds);
                }
            }
        }
    }
    // Once every view of the frame drew the shapes.
    DebugDraw::Update(m_clock.GetDeltaTime());
}

void Application::Stop() {
    Profiler::EndFrame();
    DebugDraw::Clear();
    if (const auto directory = GetUserDataDirectory(); !directory.empty()) {
        const auto result = SaveData(directory).Save(
            "console.json", 1, [](Archive& archive, std::uint32_t) { Console::Get().Serialize(archive); });
        if (!result) {
            Log::GetEngineLogger().Warning("Could not save console settings: {}", result.message);
        }
    }
    OnStop();
    for (auto& module : std::views::reverse(m_modules)) {
        module.second->OnStop();
    }
    m_pendingScene.reset();
    m_sceneChangePending = false;
    RemoveScene();
    m_pendingScene.reset();
    m_sceneChangePending = false;
    Detail::SetAssetManager(nullptr);
    m_engineConsole.reset();
    m_assets.reset();
    Time::SetClock(nullptr);
    Input::SetState(nullptr);
    Input::SetActionMap(nullptr);
    m_graphics.reset();
    m_window.reset();
    StopLog();
}

Module& Application::AddModule(std::unique_ptr<Module> module) {
    SPOT_ASSERT(module, "A module to add");
    Module& added = *module;
    AddModule(typeid(added), std::move(module));
    return added;
}

void Application::AddModule(std::type_index type, std::unique_ptr<Module> module) {
    SPOT_ASSERT(!m_running, "Modules are added before Run()");
    SPOT_ASSERT(!FindModule(type), "An application has one module of each type");
    module->m_application = this;
    m_modules.emplace_back(type, std::move(module));
}

Module* Application::FindModule(std::type_index type) const {
    const auto found = std::ranges::find(m_modules, type, [](const auto& entry) { return entry.first; });
    return found != m_modules.end() ? found->second.get() : nullptr;
}

void Application::SetGameSettings(std::optional<GameSettings> game) {
    m_config.game = std::move(game);
    m_runtimeRendering.reset();
    ++m_renderingRevision;
    ++m_gameSettingsRevision;
    m_actions = {};
    if (m_config.game) {
        m_config.game->sections.input.Apply(m_actions);
    }
    // Tools play games with the project's bindings, not a player's.
    m_actions.SetDefaults();
}

RenderingSettings Application::GetRenderingSettings() const {
    if (m_runtimeRendering)
        return *m_runtimeRendering;
    return m_config.game ? m_config.game->sections.rendering : RenderingSettings{};
}
void Application::SetRenderingSettings(RenderingSettings rendering) {
    m_runtimeRendering = std::move(rendering);
    ++m_renderingRevision;
}

bool Application::SaveInputRebinds() {
    const std::filesystem::path directory = GetUserDataDirectory();
    if (directory.empty()) {
        Log::GetEngineLogger().Error("Could not save the input rebinds: the game has no user data folder");
        return false;
    }
    return m_actions.SaveRebinds(directory / InputRebindsFileName);
}

std::filesystem::path Application::GetUserDataDirectory() {
    if (m_userDataDirectory.empty()) {
        if (!m_config.userDataDirectory.empty()) {
            std::error_code error;
            std::filesystem::create_directories(m_config.userDataDirectory, error);
            m_userDataDirectory = m_config.userDataDirectory;
        } else if (m_config.game && !m_config.game->name.empty()) {
            m_userDataDirectory = Spot::GetUserDataDirectory("Spot Games", m_config.game->name);
        }
    }
    return m_userDataDirectory;
}

void Application::Quit() {
    m_running = false;
}

void Application::SetMaxFrameRate(int maxFrameRate) {
    m_config.maxFrameRate = maxFrameRate;
    m_limiter = FrameLimiter(maxFrameRate);
}

Window& Application::GetWindow() {
    SPOT_ASSERT(m_window, "The window only exists while Run() is running");
    return *m_window;
}

GraphicsDevice& Application::GetGraphicsDevice() {
    SPOT_ASSERT(m_graphics, "The graphics device only exists while Run() is running with graphics enabled");
    return *m_graphics;
}

AssetManager& Application::GetAssets() {
    SPOT_ASSERT(m_assets, "The asset manager only exists while Run() is running");
    return *m_assets;
}

void Application::LoadScene(std::unique_ptr<Scene> scene) {
    m_pendingScene = std::move(scene);
    m_sceneChangePending = true;
}

void Application::DispatchEvent(const Event& event) {
    for (auto& [type, module] : m_modules) {
        if (module->OnEvent(event)) {
            OnEvent(event);
            if (event.Is<WindowCloseEvent>() && OnCloseRequested()) {
                Quit();
            }
            return;
        }
    }
    m_input.OnEvent(event);
    OnEvent(event);
    if (DeliversToScenes(event) && m_scene) {
        m_scene->OnEvent(event);
    }

    if (event.Is<WindowCloseEvent>() && OnCloseRequested()) {
        Quit();
    }
}

void Application::ApplySceneChanges() {
    if (!std::exchange(m_sceneChangePending, false))
        return;
    auto next = std::move(m_pendingScene);
    if (next && m_config.game) {
        m_config.game->sections.Apply(*next);
    }
    if (m_scene) {
        m_scene->OnUnload();
        if (next) {
            // The first retained ancestor carries its whole subtree, so each entity moves once.
            std::vector<EntityId> retained;
            m_scene->ForEachEntity([&](Entity& entity) {
                if (!entity.IsKeptOnSceneChange())
                    return;
                for (auto* parent = entity.GetParent(); parent; parent = parent->GetParent()) {
                    if (parent->IsKeptOnSceneChange())
                        return;
                }
                retained.push_back(entity.GetId());
            });
            for (const EntityId id : retained) {
                if (auto* entity = m_scene->GetEntity(id))
                    entity->MoveToScene(*next);
            }
        }
        m_scene->Clear();
        m_scene->m_application = nullptr;
        m_scene.reset();
    }
    m_scene = std::move(next);
    if (m_scene) {
        m_scene->m_application = this;
        m_scene->OnLoad();
    }
}

void Application::RemoveScene() {
    if (!m_scene)
        return;
    m_scene->OnUnload();
    m_scene->Clear();
    m_scene->m_application = nullptr;
    m_scene.reset();
}

bool Application::LoadStartupScene() {
    if (!m_config.game) {
        return true;
    }
    const AssetId startup = m_config.game->startupScene;
    if (!startup.IsValid()) {
        Log::GetEngineLogger().Error("'{}' has no startup scene to run", m_config.game->name);
        return false;
    }
    auto scene = InstantiateScene(*m_assets, startup);
    if (!scene) {
        Log::GetEngineLogger().Error("The startup scene of '{}' could not be loaded", m_config.game->name);
        return false;
    }
    LoadScene(std::move(scene));
    return true;
}

} // namespace Spot
