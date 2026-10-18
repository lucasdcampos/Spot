#include "runtime/application.h"
#include "scene/ui/ui_input_system.h"

#include "assets/assets.h"
#include "core/assert.h"
#include "core/crash_handler.h"
#include "core/frame_capture.h"
#include "core/log.h"
#include "core/log_sinks.h"
#include "core/paths.h"
#include "core/process.h"
#include "core/profile_stream.h"
#include "core/profiler.h"
#include "core/save_data.h"
#include "graphics/debug_draw.h"
#include "graphics/font.h"
#include "graphics/renderer_2d.h"
#include "graphics/screenshot.h"
#include "input/input.h"
#include "platform/platform.h"
#include "runtime/console_input.h"
#include "runtime/console_overlay.h"
#include "runtime/engine_console.h"
#include "runtime/game_module.h"
#include "runtime/runtime_stats.h"
#include "scene/audio/audio_system.h"
#include "scene/rendering/scene_renderer.h"
#include "scene/scene_assets.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <system_error>
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
} // namespace

Application::Application(ApplicationConfig config) : m_config(std::move(config)) {
    m_profileStreamEnabled = Process::GetEnvironmentValue("SPOT_PROFILE_STREAM") == "1";
    if (!m_config.game) {
        const auto file = GetExecutableDirectory() / GameSettings::FileName;
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error)) {
            m_config.game = GameSettings::LoadFromFile(file);
        }
    }
    // Only the default title gives way to the game's name, so applications can still name their window.
    if (m_config.game && !m_config.game->name.empty() && m_config.window.title == WindowConfig{}.title) {
        m_config.window.title = m_config.game->name;
    }
    // A game that crashes where it was shipped leaves what led to it.
    if (m_config.game && m_config.logFile.empty()) {
        m_config.logFile = GetExecutableDirectory() / "game.log";
    }
    if (!m_config.logFile.empty()) {
        std::error_code error;
        if (std::filesystem::exists(m_config.logFile, error)) {
            std::filesystem::path previous = m_config.logFile;
            previous.replace_extension(".previous" + PathToUtf8(m_config.logFile.extension()));
            std::filesystem::rename(m_config.logFile, previous, error);
        }
        auto file = std::make_shared<FileSink>(m_config.logFile);
        if (file->IsOpen()) {
            m_logFile = std::move(file);
            Log::AddSink(m_logFile);
        } else {
            Log::GetEngineLogger().Warning("Could not open log file '{}'", PathToUtf8(m_config.logFile));
        }
    }
    std::filesystem::path reports = m_config.crashReportDirectory;
    if (reports.empty() && m_logFile) {
        reports = std::filesystem::absolute(m_config.logFile).parent_path() / "crashes";
    }
    InstallCrashHandler({.applicationName = m_config.window.title,
                         .reportDirectory = reports,
                         .showMessage = m_config.showCrashMessage});
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
        m_frameCapture = FrameCapture::Connect();
        m_graphics = GraphicsDevice::Create(*m_window, m_config.graphics);
        if (!m_graphics) {
            m_window.reset();
            return EXIT_FAILURE;
        }
    }

    m_audio = std::make_unique<AudioDevice>(m_config.audio);
    if (m_config.game) {
        m_config.game->sections.audio.Apply(*m_audio);
    }

    std::filesystem::path importDirectory = m_config.assets.importDirectory;
    if (importDirectory.empty()) {
        importDirectory = Utf8ToPath(m_config.game ? m_config.game->importDirectory : "imported");
    }
    m_assets = std::make_unique<AssetManager>(GetExecutableDirectory() / importDirectory, m_graphics.get());
    Detail::SetAssetManager(m_assets.get());

    Input::SetState(&m_input);
    Input::SetActionMap(&m_actions);
    Audio::SetDevice(m_audio.get());
    Time::SetClock(&m_clock);
    m_limiter = FrameLimiter(m_config.maxFrameRate);
    m_clock.Start(std::chrono::steady_clock::now());
    m_persistentScene = std::make_unique<Scene>();
    m_persistentScene->m_application = this;
    if (m_config.game) {
        m_config.game->sections.Apply(*m_persistentScene);
        m_config.game->sections.input.Apply(m_actions);
    }
    m_actions.SetDefaults();
    // Only games with actions have rebinds, so other applications never make a user data folder.
    if (m_config.game && !m_actions.GetActions().empty()) {
        if (const std::filesystem::path directory = GetUserDataDirectory(); !directory.empty()) {
            m_actions.LoadRebinds(directory / InputRebindsFileName);
        }
    }
    m_engineConsole = std::make_unique<EngineConsole>(Console::Get(), *this);
    if (m_config.consoleOverlay) {
        m_consoleInput = std::make_unique<ConsoleInput>(Console::Get(), m_input);
    }
    if (const auto directory = GetUserDataDirectory(); !directory.empty()) {
        const auto result = SaveData(directory).Load(
            "console.json", 1, [](Archive& archive, std::uint32_t) { Console::Get().Serialize(archive); });
        if (!result && result.status != SaveDataStatus::NotFound) {
            Log::GetEngineLogger().Warning("Could not load console settings: {}", result.message);
        }
    }
    m_running = true;
    m_failed = false;
    // The game's components are known before its scenes load.
    Detail::GameCode::Load();
    OnStart();
    if (m_running && m_scenes.empty() && m_sceneChanges.empty() && !LoadStartupScene()) {
        m_failed = true;
        Quit();
    }

    while (m_running) {
        // Ends the last frame's profile, so the one profiled spans the whole frame, waiting included.
        Profiler::BeginFrame();
        StreamProfileFrame();
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
            break;
        }

        {
            SPOT_PROFILE_SCOPE("Application::LoadScenes");
            ApplySceneChanges();
        }
        {
            SPOT_PROFILE_SCOPE("Application::OnUpdate");
            OnUpdate(m_clock.GetDeltaTime());
        }
        if (!m_scenesPaused || std::exchange(m_stepScenes, false)) {
            m_persistentScene->Update(m_clock.GetDeltaTime());
            for (const auto& scene : m_scenes) {
                scene->Update(m_clock.GetDeltaTime());
            }
        }
        if (Profiler::IsEnabled()) {
            SPOT_PROFILE_SCOPE("Developer::Stats");
            const auto stats = GetRuntimeStats();
            Profiler::Count("Bodies", static_cast<double>(stats.bodies));
            Profiler::Count("Voices", static_cast<double>(stats.voices));
            Profiler::Count("Resident memory", static_cast<double>(stats.memoryBytes));
        }
        UpdateUiTextInput();
        // Only changes are applied, so applications can still capture the mouse themselves.
        if (m_input.IsCursorLockApplied() != m_cursorLocked) {
            m_cursorLocked = m_input.IsCursorLockApplied();
            m_window->SetMouseCaptured(m_cursorLocked);
        }

        if (m_graphics) {
            SPOT_PROFILE_SCOPE("Application::Render");
            if (RenderContext* context = m_graphics->BeginFrame()) {
                context->BeginGpuTimer("Frame");
                const bool screenshot = PrepareScreenshot(*context);
                m_persistentScene->PrepareToRender();
                for (const auto& scene : m_scenes) {
                    scene->PrepareToRender();
                }
                m_persistentScene->OnRender(*context);
                for (const auto& scene : m_scenes) {
                    scene->OnRender(*context);
                }
                RenderScenes(*context);
                {
                    SPOT_PROFILE_SCOPE("Application::OnRender");
                    OnRender(*context);
                }
                RenderDeveloperOverlay(*context);
                if (screenshot) {
                    context->SetFrameTarget(nullptr);
                    context->BlitTextureToFrame(*m_screenshotTexture);
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
                if (screenshot) {
                    const bool saved = SaveScreenshot(*m_graphics, *m_screenshotTexture, m_screenshotPath);
                    Log::GetEngineLogger().Write(saved ? LogLevel::Info : LogLevel::Warning, "Screenshot {}: {}",
                                                 saved ? "saved" : "failed", PathToUtf8(m_screenshotPath));
                    m_screenshotPath.clear();
                    m_screenshotTexture.reset();
                }
            }
        }
        // Once every view of the frame drew the shapes.
        DebugDraw::Update(m_clock.GetDeltaTime());
    }

    Profiler::EndFrame();
    StreamProfileFrame();
    DebugDraw::Clear();
    if (const auto directory = GetUserDataDirectory(); !directory.empty()) {
        const auto result = SaveData(directory).Save(
            "console.json", 1, [](Archive& archive, std::uint32_t) { Console::Get().Serialize(archive); });
        if (!result) {
            Log::GetEngineLogger().Warning("Could not save console settings: {}", result.message);
        }
    }
    OnStop();
    m_consoleInput.reset();
    m_developerRenderer.reset();
    m_developerFont = {};
    m_sceneChanges.clear();
    RemoveAllScenes();
    m_persistentScene->Clear();
    m_persistentScene.reset();
    m_sceneRenderer.reset();
    Detail::SetAssetManager(nullptr);
    m_engineConsole.reset();
    m_assets.reset();
    Time::SetClock(nullptr);
    Input::SetState(nullptr);
    Input::SetActionMap(nullptr);
    Audio::SetDevice(nullptr);
    m_audio.reset();
    m_screenshotTexture.reset();
    m_screenshotPath.clear();
    m_graphics.reset();
    m_frameCapture.reset();
    m_window.reset();
    return m_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

void Application::SetScenesPaused(bool paused) {
    m_scenesPaused = paused;
    if (m_persistentScene) {
        if (auto* audio = m_persistentScene->GetSystem<AudioSystem>()) {
            audio->SetPaused(paused);
        }
    }
    for (const auto& scene : m_scenes) {
        if (auto* audio = scene->GetSystem<AudioSystem>()) {
            audio->SetPaused(paused);
        }
    }
}

void Application::SetGameSettings(std::optional<GameSettings> game) {
    m_config.game = std::move(game);
    m_runtimeRendering.reset();
    ++m_renderingRevision;
    // Made again with the game's rendering settings when next needed.
    m_sceneRenderer.reset();
    m_actions = {};
    if (m_config.game) {
        m_config.game->sections.input.Apply(m_actions);
        if (m_audio) {
            m_config.game->sections.audio.Apply(*m_audio);
        }
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
    m_renderingDirty = true;
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

AudioDevice& Application::GetAudioDevice() {
    SPOT_ASSERT(m_audio, "The audio device only exists while Run() is running");
    return *m_audio;
}

void Application::LoadScene(std::unique_ptr<Scene> scene) {
    m_sceneChanges.clear();
    m_sceneChanges.push_back({.kind = SceneChangeKind::Replace, .scene = std::move(scene)});
}

void Application::LoadSceneAdditive(std::unique_ptr<Scene> scene) {
    SPOT_ASSERT(scene, "Only a scene can be loaded");
    m_sceneChanges.push_back({.kind = SceneChangeKind::Add, .scene = std::move(scene)});
}

void Application::UnloadScene(Scene& scene) {
    SPOT_ASSERT(&scene != m_persistentScene.get(), "The persistent scene cannot be unloaded");
    const auto pending = std::ranges::find_if(
        m_sceneChanges, [&scene](const SceneChange& change) { return change.scene.get() == &scene; });
    if (pending != m_sceneChanges.end()) {
        if (pending->kind == SceneChangeKind::Replace) {
            pending->scene.reset();
        } else {
            m_sceneChanges.erase(pending);
        }
        return;
    }

    // The scene is loaded. A queued replacement unloads it anyway, and it may already be queued for unloading.
    const bool unloading = std::ranges::any_of(m_sceneChanges, [&scene](const SceneChange& change) {
        return change.kind == SceneChangeKind::Replace ||
               (change.kind == SceneChangeKind::Remove && change.target == &scene);
    });
    if (!unloading) {
        m_sceneChanges.push_back({.kind = SceneChangeKind::Remove, .scene = nullptr, .target = &scene});
    }
}

std::vector<Scene*> Application::GetScenes() const {
    std::vector<Scene*> scenes;
    scenes.reserve(m_scenes.size());
    for (const auto& scene : m_scenes) {
        scenes.push_back(scene.get());
    }
    return scenes;
}

Scene& Application::GetPersistentScene() {
    SPOT_ASSERT(m_persistentScene, "The persistent scene only exists while Run() is running");
    return *m_persistentScene;
}

void Application::MakePersistent(Entity& entity) {
    entity.MoveToScene(GetPersistentScene());
}

void Application::DispatchEvent(const Event& event) {
    const bool wasOpen = m_consoleInput && m_consoleInput->IsOpen();
    bool consumed = m_consoleInput && m_consoleInput->OnEvent(event);
    if (m_window && m_consoleInput && wasOpen != m_consoleInput->IsOpen()) {
        if (m_consoleInput->IsOpen()) {
            m_window->StartTextInput();
        } else {
            m_window->StopTextInput();
        }
    }
    if (!consumed && DeliversToScenes(event) && m_window) {
        Event uiEvent = event;
        if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
            auto pixel = *moved;
            const float xScale =
                static_cast<float>(m_window->GetPixelWidth()) / static_cast<float>(std::max(1, m_window->GetWidth()));
            const float yScale =
                static_cast<float>(m_window->GetPixelHeight()) / static_cast<float>(std::max(1, m_window->GetHeight()));
            pixel.position = {pixel.position.x * xScale, pixel.position.y * yScale};
            uiEvent = Event(pixel);
        }
        const Vector2 size{static_cast<float>(m_window->GetPixelWidth()),
                           static_cast<float>(m_window->GetPixelHeight())};
        for (auto scene = m_scenes.rbegin(); scene != m_scenes.rend() && !consumed; ++scene)
            consumed = (*scene)->HandleUiEvent(uiEvent, size, &m_input);
        if (!consumed && m_persistentScene)
            consumed = m_persistentScene->HandleUiEvent(uiEvent, size, &m_input);
        if (consumed)
            m_input.ConsumeEvent(event);
    }
    if (!consumed) {
        m_input.OnEvent(event);
    }
    UpdateUiTextInput();
    OnEvent(event);
    if (!consumed && DeliversToScenes(event)) {
        if (m_persistentScene) {
            m_persistentScene->OnEvent(event);
        }
        for (const auto& scene : m_scenes) {
            scene->OnEvent(event);
        }
    }

    if (event.Is<WindowCloseEvent>() && OnCloseRequested()) {
        Quit();
    }
}

void Application::UpdateUiTextInput() {
    if (!m_window)
        return;
    const bool wanted = UiInputSystem::IsTextInputCaptured(m_input);
    if (wanted && !m_window->IsTextInputActive())
        m_window->StartTextInput();
    else if (m_uiTextInput && !wanted && !(m_consoleInput && m_consoleInput->IsOpen()))
        m_window->StopTextInput();
    m_uiTextInput = wanted;
}

void Application::ApplySceneChanges() {
    // Taken out first, so changes asked for by OnLoad() and OnUnload() wait for the next frame.
    auto changes = std::exchange(m_sceneChanges, {});
    for (SceneChange& change : changes) {
        switch (change.kind) {
        case SceneChangeKind::Replace:
            RemoveAllScenes();
            if (change.scene) {
                AddScene(std::move(change.scene));
            }
            break;
        case SceneChangeKind::Add:
            AddScene(std::move(change.scene));
            break;
        case SceneChangeKind::Remove:
            RemoveScene(change.target);
            break;
        }
    }
}

void Application::AddScene(std::unique_ptr<Scene> scene) {
    Scene& added = *scene;
    added.m_application = this;
    if (m_config.game) {
        m_config.game->sections.Apply(added);
    }
    m_scenes.push_back(std::move(scene));
    added.OnLoad();
}

void Application::RemoveScene(const Scene* scene) {
    // Only compared until found, since the scene may have been unloaded by an earlier change.
    const auto found = std::ranges::find_if(m_scenes, [scene](const auto& loaded) { return loaded.get() == scene; });
    if (found == m_scenes.end()) {
        return;
    }
    Scene& removed = **found;
    removed.OnUnload();
    removed.Clear();
    removed.m_application = nullptr;
    m_scenes.erase(found);
}

RuntimeStats Application::GetRuntimeStats() const {
    auto scenes = GetScenes();
    if (m_persistentScene) {
        scenes.push_back(m_persistentScene.get());
    }
    return RuntimeStats::Sample(scenes, m_audio.get());
}

std::filesystem::path Application::GetCaptureDirectory() {
    auto directory = GetUserDataDirectory();
    if (directory.empty()) {
        directory = Spot::GetUserDataDirectory("Spot", "Developer");
    }
    return directory.empty() ? directory : directory / "screenshots";
}

bool Application::RequestScreenshot(std::string_view fileName) {
    const std::string name = fileName.empty() ? CaptureName("screenshot", ".png") : std::string(fileName);
    if (!IsCaptureName(name) || !name.ends_with(".png") || !m_graphics || !m_screenshotPath.empty()) {
        return false;
    }
    const auto directory = GetCaptureDirectory();
    if (directory.empty()) {
        return false;
    }
    m_screenshotPath = directory / Utf8ToPath(name);
    return true;
}

bool Application::RequestFrameCapture(std::string_view fileName) {
    const std::string name = fileName.empty() ? CaptureName("frame", "") : std::string(fileName);
    if (!IsCaptureName(name) || !m_graphics || !m_frameCapture) {
        return false;
    }
    const auto directory = GetCaptureDirectory();
    return !directory.empty() && m_frameCapture->Request(directory / Utf8ToPath(name));
}

bool Application::PrepareScreenshot(RenderContext& context) {
    if (m_screenshotPath.empty()) {
        return false;
    }
    m_screenshotTexture = m_graphics->CreateTexture({.width = static_cast<std::uint32_t>(context.GetWidth()),
                                                     .height = static_cast<std::uint32_t>(context.GetHeight()),
                                                     .format = m_graphics->GetWindowFormat(),
                                                     .renderTarget = true});
    if (!m_screenshotTexture || !context.SetFrameTarget(m_screenshotTexture.get())) {
        Log::GetEngineLogger().Warning("Could not create the screenshot target");
        m_screenshotTexture.reset();
        m_screenshotPath.clear();
        return false;
    }
    auto clear = context.BeginRenderPass({.clearColor = Color::Black});
    return true;
}

void Application::StreamProfileFrame() {
    if (!m_profileStreamEnabled || !Profiler::IsEnabled()) {
        return;
    }
    const auto frames = Profiler::GetFrames();
    if (!frames.empty() && frames.back()->number != m_lastStreamedFrame) {
        m_lastStreamedFrame = frames.back()->number;
        Log::GetEngineLogger().Info("{}", EncodeProfileFrame(*frames.back()));
    }
}

void Application::RenderDeveloperOverlay(RenderContext& context) {
    const bool consoleOpen = m_consoleInput && m_consoleInput->IsOpen();
    if ((!consoleOpen && !m_statsVisible) || !m_developerFont) {
        return;
    }
    if (!m_developerRenderer) {
        m_developerRenderer = std::make_unique<Renderer2D>(*m_graphics);
    }
    if (!m_developerRenderer->IsValid()) {
        return;
    }
    const Vector2 size{static_cast<float>(context.GetWidth()), static_cast<float>(context.GetHeight())};
    auto pass =
        context.BeginRenderPass({.clearColor = m_config.renderScenes ? std::nullopt : std::optional(Color::Black)});
    m_developerRenderer->Begin(pass, Camera2D{.position = size * 0.5f}, size);
    if (m_statsVisible) {
        DrawStatsOverlay(GetRuntimeStats(), *m_developerFont, *m_developerRenderer, size);
    }
    if (consoleOpen) {
        DrawConsoleOverlay(Console::Get(), *m_consoleInput, *m_developerFont, *m_developerRenderer, size);
    }
    m_developerRenderer->End();
}

void Application::RenderScenes(RenderContext& context) {
    if (!m_config.renderScenes || (m_scenes.empty() && m_persistentScene->GetEntityCount() == 0)) {
        return;
    }
    if (m_renderingDirty) {
        m_sceneRenderer.reset();
        m_renderingDirty = false;
    }
    if (!m_sceneRenderer) {
        SceneRendererConfig config;
        GetRenderingSettings().Apply(config);
        m_sceneRenderer = std::make_unique<SceneRenderer>(*m_graphics, config);
    }
    std::vector<Scene*> scenes = GetScenes();
    scenes.insert(scenes.begin(), m_persistentScene.get());
    m_sceneRenderer->Render(context, scenes);
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

void Application::RemoveAllScenes() {
    // The newest first, like destructors, so later scenes can still rely on earlier ones.
    while (!m_scenes.empty()) {
        RemoveScene(m_scenes.back().get());
    }
}

} // namespace Spot
