#include "graphics/scene_renderer.h"

#include "core/profiler.h"

#include "core/log.h"
#include "graphics/debug_draw.h"
#include "graphics/graphics_device.h"
#include "graphics/line_renderer.h"
#include "graphics/render_context.h"
#include "graphics/renderer_2d.h"
#include "graphics/renderer_3d.h"
#include "scene/camera.h"
#include "scene/post_processing.h"
#include "scene/scene.h"
#include "scene/sprite_renderer.h"

namespace Spot {

namespace {

constexpr TextureFormat HdrFormat = TextureFormat::Rgba16Float;

} // namespace

SceneRenderer::SceneRenderer(GraphicsDevice& device, const SceneRendererConfig& config)
    : m_device(device), m_format(config.targetFormat.value_or(device.GetWindowFormat())), m_sampleCount(1),
      m_background(config.background), m_tonemapping(config.tonemapping), m_grading(config.grading),
      m_bloomSettings(config.bloom), m_fxaaEnabled(config.fxaa) {
    if (config.sampleCount > 1 && device.SupportsSampleCount(HdrFormat, config.sampleCount) &&
        device.SupportsSampleCount(TextureFormat::Depth32Float, config.sampleCount)) {
        m_sampleCount = config.sampleCount;
    }
    m_renderer3D =
        std::make_unique<Renderer3D>(device, Renderer3DConfig{.targetFormat = HdrFormat,
                                                              .sampleCount = m_sampleCount,
                                                              .hdr = true,
                                                              .shadowMapSize = config.shadowMapSize,
                                                              .shadowCascadeCount = config.shadowCascadeCount,
                                                              .shadowDistance = config.shadowDistance,
                                                              .localShadowMapSize = config.localShadowMapSize});
    m_sprites =
        std::make_unique<Renderer2D>(device, Renderer2DConfig{.targetFormat = HdrFormat, .sampleCount = m_sampleCount});
    m_interface = std::make_unique<Renderer2D>(device, Renderer2DConfig{.targetFormat = m_format});
    m_lines = std::make_unique<LineRenderer>(
        device, LineRendererConfig{.targetFormat = HdrFormat, .sampleCount = m_sampleCount});
    m_tonemapper = Tonemapper::Create(device, m_format);
    m_bloom = Bloom::Create(device, HdrFormat);
    m_fxaa = Fxaa::Create(device, m_format);
}

SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::IsValid() const {
    return m_renderer3D->IsValid() && m_sprites->IsValid() && m_interface->IsValid() && m_lines->IsValid() &&
           m_tonemapper && m_bloom && m_fxaa;
}

void SceneRenderer::Render(RenderContext& context, std::span<Scene* const> scenes, Texture* target) {
    SPOT_PROFILE_SCOPE("SceneRenderer::Render");
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

    // With several samples, each pass draws into the multisampled target and resolves it into the HDR image, keeping
    // the samples for the passes drawn over it.
    Texture* samples = m_samples.get();
    const bool lines = !DebugDraw::GetLines().empty();
    const auto pass = [&](bool more) {
        return RenderPassDesc{.target = samples ? samples : m_hdr.get(),
                              .resolveTarget = samples ? m_hdr.get() : nullptr,
                              .keepSamples = samples && more};
    };

    RenderPassDesc meshes = pass(lines || sprites);
    meshes.clearColor = camera ? camera->GetBackground() : m_background;
    meshes.depthTarget = m_depth.get();
    meshes.clearDepth = 1.0f;
    m_renderer3D->Begin(context, meshes, view);
    for (Scene* scene : scenes) {
        m_renderer3D->DrawScene(*scene);
    }
    m_renderer3D->End();

    const Vector2 size{static_cast<float>(width), static_cast<float>(height)};
    if (lines) {
        // Over the meshes, hidden by them where they test depth.
        RenderPassDesc linesDesc = pass(sprites);
        linesDesc.depthTarget = m_depth.get();
        auto linePass = context.BeginRenderPass(linesDesc);
        m_lines->Begin(linePass, view.GetViewProjection(size));
        m_lines->DrawDebugLines();
        m_lines->End();
    }
    // Sprites go over the meshes, as 2D games draw them, without a depth target.
    if (sprites) {
        auto spritePass = context.BeginRenderPass(pass(false));
        m_sprites->Begin(spritePass, view.GetViewProjection(size));
        for (Scene* scene : scenes) {
            m_sprites->DrawScene(*scene);
        }
        m_sprites->End();
    }

    const PostProcessing* look = PostProcessing::Find(scenes);
    const TonemapSettings tonemapping = look ? look->GetTonemapping() : m_tonemapping;
    const BloomSettings bloom = look ? look->GetBloom() : m_bloomSettings;
    const GradingSettings grading = look ? look->GetGrading() : m_grading;
    const Texture* glow = m_bloom->Render(context, *m_hdr, bloom);
    // FXAA reads the finished image, so it is tonemapped into a texture first.
    const bool fxaa = m_fxaaEnabled && UpdateTonemapped(width, height);
    if (fxaa) {
        auto tonemapped = context.BeginRenderPass({.target = m_tonemapped.get()});
        m_tonemapper->Draw(tonemapped, *m_hdr, tonemapping, grading, glow, bloom.intensity);
    }
    // The last pass covers the whole target, and the interfaces go over it as they are.
    auto output = context.BeginRenderPass({.target = target});
    if (fxaa) {
        m_fxaa->Draw(output, *m_tonemapped);
    } else {
        m_tonemapper->Draw(output, *m_hdr, tonemapping, grading, glow, bloom.intensity);
    }
    m_interface->Begin(output, Scene::ScreenCamera(size), size);
    for (Scene* scene : scenes) {
        scene->DrawUi(*m_interface, size);
    }
    m_interface->End();
}

bool SceneRenderer::UpdateTonemapped(std::uint32_t width, std::uint32_t height) {
    if (!m_tonemapped || m_tonemapped->GetWidth() != width || m_tonemapped->GetHeight() != height) {
        m_tonemapped =
            m_device.CreateTexture({.width = width, .height = height, .format = m_format, .renderTarget = true});
    }
    return m_tonemapped != nullptr;
}

bool SceneRenderer::UpdateTargets(std::uint32_t width, std::uint32_t height) {
    if (!m_depth || m_depth->GetWidth() != width || m_depth->GetHeight() != height) {
        m_hdr = m_device.CreateTexture({.width = width, .height = height, .format = HdrFormat, .renderTarget = true});
        m_depth = m_device.CreateTexture({.width = width,
                                          .height = height,
                                          .format = TextureFormat::Depth32Float,
                                          .renderTarget = true,
                                          .sampleCount = m_sampleCount});
        m_samples = m_sampleCount > 1 ? m_device.CreateTexture({.width = width,
                                                                .height = height,
                                                                .format = HdrFormat,
                                                                .renderTarget = true,
                                                                .sampleCount = m_sampleCount})
                                      : nullptr;
    }
    return m_hdr && m_depth && (m_sampleCount == 1 || m_samples);
}

} // namespace Spot
