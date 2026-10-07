#include "core/application.h"

#include "assets/assets.h"
#include "core/assert.h"
#include "core/log.h"
#include "core/log_sinks.h"
#include "core/paths.h"
#include "input/input.h"
#include "platform/platform.h"

#include <algorithm>
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

    m_audio = std::make_unique<AudioDevice>(m_config.audio);

    const auto executableDirectory = GetExecutableDirectory();
    const auto importDirectory = executableDirectory / m_config.assets.importDirectory;
    if (!m_config.assets.sourceDirectory.empty()) {
        const auto sourceDirectory = executableDirectory / m_config.assets.sourceDirectory;
        if (std::filesystem::is_directory(sourceDirectory)) {
            m_assetDatabase = std::make_unique<AssetDatabase>(sourceDirectory, importDirectory);
            m_assetDatabase->Refresh();
        }
    }
    m_assets = std::make_unique<AssetManager>(importDirectory, m_graphics.get());
    Detail::SetAssetManager(m_assets.get());

    Input::SetState(&m_input);
    Time::SetClock(&m_clock);
    m_limiter = FrameLimiter(m_config.maxFrameRate);
    m_clock.Start(std::chrono::steady_clock::now());
    m_persistentScene = std::make_unique<Scene>();
    m_persistentScene->m_application = this;
    m_running = true;
    OnStart();

    while (m_running) {
        Platform::SleepFor(m_limiter.GetWaitTime(std::chrono::steady_clock::now()));
        m_clock.Tick(std::chrono::steady_clock::now());

        m_input.BeginFrame();
        Platform::PollEvents([this](const Event& event) { DispatchEvent(event); });
        if (!m_running) {
            break;
        }

        ApplySceneChanges();
        OnUpdate(m_clock.GetDeltaTime());
        if (!m_scenesPaused || std::exchange(m_stepScenes, false)) {
            m_persistentScene->Update(m_clock.GetDeltaTime());
            for (const auto& scene : m_scenes) {
                scene->Update(m_clock.GetDeltaTime());
            }
        }

        if (m_graphics) {
            if (RenderContext* context = m_graphics->BeginFrame()) {
                m_persistentScene->OnRender(*context);
                for (const auto& scene : m_scenes) {
                    scene->OnRender(*context);
                }
                OnRender(*context);
                m_graphics->EndFrame();
            }
        }
    }

    OnStop();
    m_sceneChanges.clear();
    RemoveAllScenes();
    m_persistentScene->Clear();
    m_persistentScene.reset();
    Detail::SetAssetManager(nullptr);
    m_assets.reset();
    m_assetDatabase.reset();
    Time::SetClock(nullptr);
    Input::SetState(nullptr);
    m_audio.reset();
    m_graphics.reset();
    m_window.reset();
    return EXIT_SUCCESS;
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
    m_input.OnEvent(event);
    OnEvent(event);
    if (DeliversToScenes(event)) {
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

void Application::RemoveAllScenes() {
    // The newest first, like destructors, so later scenes can still rely on earlier ones.
    while (!m_scenes.empty()) {
        RemoveScene(m_scenes.back().get());
    }
}

} // namespace Spot
