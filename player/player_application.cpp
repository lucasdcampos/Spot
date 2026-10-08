#include "player_application.h"

#include <cstdint>
#include <vector>

namespace Spot::Player {

Logger& GetPlayerLogger() {
    static Logger logger("Player");
    return logger;
}

namespace {

ApplicationConfig MakeConfig(const Project& project, const PlayerOptions& options) {
    // The sources are imported first, as the editor would, so the game runs what was saved last.
    return {
        .window = {.title = project.GetSettings().name},
        .graphics = {.enabled = options.graphics},
        .assets = {.importDirectory = project.GetImportDirectory(), .sourceDirectory = project.GetAssetsDirectory()},
    };
}

} // namespace

PlayerApplication::PlayerApplication(const Project& project, PlayerOptions options)
    : Application(MakeConfig(project, options)), m_project(project), m_options(options) {
}

void PlayerApplication::OnStart() {
    const AssetId startup = m_project.GetSettings().startupScene;
    if (!startup.IsValid()) {
        GetPlayerLogger().Error("'{}' has no startup scene to run; pick one in the editor's project settings",
                                m_project.GetSettings().name);
        Quit();
        return;
    }
    auto scene = GetAssets().InstantiateScene(startup);
    if (!scene) {
        GetPlayerLogger().Error("The startup scene of '{}' could not be loaded", m_project.GetSettings().name);
        Quit();
        return;
    }
    LoadScene(std::move(scene));
    GetPlayerLogger().Info("Running '{}'", m_project.GetSettings().name);

    if (m_options.graphics) {
        GraphicsDevice& device = GetGraphicsDevice();
        // Four samples per pixel smooth edges, where the GPU supports them.
        const bool multisampled = device.SupportsSampleCount(device.GetWindowFormat(), 4) &&
                                  device.SupportsSampleCount(TextureFormat::Depth32Float, 4);
        m_sampleCount = multisampled ? 4 : 1;
        m_renderer3D = std::make_unique<Renderer3D>(device, Renderer3DConfig{.sampleCount = m_sampleCount});
        m_renderer2D = std::make_unique<Renderer2D>(device, Renderer2DConfig{.sampleCount = m_sampleCount});
    }
}

void PlayerApplication::OnUpdate(float) {
    if (m_options.frames > 0 && ++m_frame >= m_options.frames) {
        Quit();
    }
}

void PlayerApplication::OnRender(RenderContext& context) {
    if (!m_renderer3D || !UpdateTargets(context)) {
        return;
    }
    std::vector<Scene*> scenes = GetScenes();
    scenes.insert(scenes.begin(), &GetPersistentScene());

    const Camera* camera = nullptr;
    for (Scene* scene : scenes) {
        if (!camera) {
            camera = FindMainCamera(*scene);
        }
    }
    if (!camera && !scenes.empty() && !m_warnedNoCamera) {
        m_warnedNoCamera = true;
        GetPlayerLogger().Warning("The scene has no enabled Camera to be seen through; it shows from the origin");
    }
    const Camera3D view = camera ? camera->GetCamera3D() : Camera3D{};
    bool sprites = false;
    for (const Scene* scene : scenes) {
        sprites = sprites || scene->FindComponent<SpriteRenderer>();
    }

    Texture* samples = m_samples.get();
    m_renderer3D->Begin(context,
                        {.target = samples,
                         .clearColor = camera ? camera->GetBackground() : Color{0.12f, 0.13f, 0.15f, 1.0f},
                         .depthTarget = m_depth.get(),
                         .clearDepth = 1.0f,
                         .resolveToWindow = samples != nullptr,
                         .keepSamples = samples && sprites},
                        view);
    for (Scene* scene : scenes) {
        m_renderer3D->DrawScene(*scene);
    }
    m_renderer3D->End();

    // Sprites go over the meshes, as 2D games draw them, without a depth target.
    if (sprites && m_renderer2D->IsValid()) {
        auto pass = context.BeginRenderPass({.target = samples, .resolveToWindow = samples != nullptr});
        m_renderer2D->Begin(pass, view.GetViewProjection({static_cast<float>(context.GetWidth()),
                                                          static_cast<float>(context.GetHeight())}));
        for (Scene* scene : scenes) {
            m_renderer2D->DrawScene(*scene);
        }
        m_renderer2D->End();
    }
}

bool PlayerApplication::UpdateTargets(RenderContext& context) {
    GraphicsDevice& device = GetGraphicsDevice();
    const auto width = static_cast<std::uint32_t>(context.GetWidth());
    const auto height = static_cast<std::uint32_t>(context.GetHeight());
    if (!m_depth || m_depth->GetWidth() != width || m_depth->GetHeight() != height) {
        m_depth = device.CreateTexture({.width = width,
                                        .height = height,
                                        .format = TextureFormat::Depth32Float,
                                        .renderTarget = true,
                                        .sampleCount = m_sampleCount});
        m_samples = m_sampleCount > 1 ? device.CreateTexture({.width = width,
                                                              .height = height,
                                                              .format = device.GetWindowFormat(),
                                                              .renderTarget = true,
                                                              .sampleCount = m_sampleCount})
                                      : nullptr;
    }
    return m_depth && (m_sampleCount == 1 || m_samples);
}

} // namespace Spot::Player
