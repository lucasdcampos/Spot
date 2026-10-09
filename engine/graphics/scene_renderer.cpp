#include "graphics/scene_renderer.h"

#include "core/log.h"
#include "graphics/graphics_device.h"
#include "graphics/render_context.h"
#include "graphics/renderer_2d.h"
#include "graphics/renderer_3d.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/sprite_renderer.h"

namespace Spot {

SceneRenderer::SceneRenderer(GraphicsDevice& device, const SceneRendererConfig& config)
    : m_device(device), m_format(config.targetFormat.value_or(device.GetWindowFormat())), m_sampleCount(1),
      m_background(config.background) {
    if (config.sampleCount > 1 && device.SupportsSampleCount(m_format, config.sampleCount) &&
        device.SupportsSampleCount(TextureFormat::Depth32Float, config.sampleCount)) {
        m_sampleCount = config.sampleCount;
    }
    m_renderer3D = std::make_unique<Renderer3D>(
        device, Renderer3DConfig{.targetFormat = config.targetFormat, .sampleCount = m_sampleCount});
    m_renderer2D = std::make_unique<Renderer2D>(
        device, Renderer2DConfig{.targetFormat = config.targetFormat, .sampleCount = m_sampleCount});
}

SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::IsValid() const {
    return m_renderer3D->IsValid() && m_renderer2D->IsValid();
}

void SceneRenderer::Render(RenderContext& context, std::span<Scene* const> scenes, Texture* target) {
    const auto width = static_cast<std::uint32_t>(target ? target->GetWidth() : context.GetWidth());
    const auto height = static_cast<std::uint32_t>(target ? target->GetHeight() : context.GetHeight());
    if (!IsValid() || width == 0 || height == 0 || !UpdateTargets(width, height)) {
        return;
    }

    const Camera* camera = nullptr;
    bool sprites = false;
    for (Scene* scene : scenes) {
        camera = camera ? camera : FindMainCamera(*scene);
        sprites = sprites || scene->FindComponent<SpriteRenderer>();
    }
    if (!camera && !scenes.empty() && !m_warnedNoCamera) {
        m_warnedNoCamera = true;
        Log::GetEngineLogger().Warning("The scene has no enabled Camera to be seen through; it shows from the origin");
    }
    const Camera3D view = camera ? camera->GetCamera3D() : Camera3D{};

    // With several samples, each pass draws into the multisampled target and resolves it into the real one, keeping
    // the samples for the passes drawn over it.
    Texture* samples = m_samples.get();
    const auto pass = [&](bool more) {
        return RenderPassDesc{.target = samples ? samples : target,
                              .resolveTarget = samples ? target : nullptr,
                              .resolveToWindow = samples && !target,
                              .keepSamples = samples && more};
    };

    RenderPassDesc meshes = pass(true);
    meshes.clearColor = camera ? camera->GetBackground() : m_background;
    meshes.depthTarget = m_depth.get();
    meshes.clearDepth = 1.0f;
    m_renderer3D->Begin(context, meshes, view);
    for (Scene* scene : scenes) {
        m_renderer3D->DrawScene(*scene);
    }
    m_renderer3D->End();

    const Vector2 size{static_cast<float>(width), static_cast<float>(height)};
    // Sprites go over the meshes, as 2D games draw them, without a depth target.
    if (sprites) {
        auto spritePass = context.BeginRenderPass(pass(true));
        m_renderer2D->Begin(spritePass, view.GetViewProjection(size));
        for (Scene* scene : scenes) {
            m_renderer2D->DrawScene(*scene);
        }
        m_renderer2D->End();
    }

    auto uiPass = context.BeginRenderPass(pass(false));
    m_renderer2D->Begin(uiPass, Scene::ScreenCamera(size), size);
    for (Scene* scene : scenes) {
        scene->DrawUi(*m_renderer2D, size);
    }
    m_renderer2D->End();
}

bool SceneRenderer::UpdateTargets(std::uint32_t width, std::uint32_t height) {
    if (!m_depth || m_depth->GetWidth() != width || m_depth->GetHeight() != height) {
        m_depth = m_device.CreateTexture({.width = width,
                                          .height = height,
                                          .format = TextureFormat::Depth32Float,
                                          .renderTarget = true,
                                          .sampleCount = m_sampleCount});
        m_samples = m_sampleCount > 1 ? m_device.CreateTexture({.width = width,
                                                                .height = height,
                                                                .format = m_format,
                                                                .renderTarget = true,
                                                                .sampleCount = m_sampleCount})
                                      : nullptr;
    }
    return m_depth && (m_sampleCount == 1 || m_samples);
}

} // namespace Spot
