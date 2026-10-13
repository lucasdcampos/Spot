#include "graphics/scene_renderer.h"
#include "scene/tilemap.h"

#include "core/profiler.h"

#include "core/log.h"
#include "graphics/debug_draw.h"
#include "graphics/graphics_device.h"
#include "graphics/line_renderer.h"
#include "graphics/particle_renderer.h"
#include "graphics/render_context.h"
#include "graphics/renderer_2d.h"
#include "graphics/renderer_3d.h"
#include "particles/particle_system.h"
#include "scene/camera.h"
#include "scene/post_processing.h"
#include "scene/scene.h"
#include "scene/sprite_renderer.h"
#include "scene/text_renderer.h"

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
    m_particles = std::make_unique<ParticleRenderer>(
        device, ParticleRendererConfig{.targetFormat = HdrFormat, .sampleCount = m_sampleCount});
    m_interface = std::make_unique<Renderer2D>(device, Renderer2DConfig{.targetFormat = m_format});
    m_lines = std::make_unique<LineRenderer>(
        device, LineRendererConfig{.targetFormat = HdrFormat, .sampleCount = m_sampleCount});
    m_tonemapper = Tonemapper::Create(device, m_format);
    m_bloom = Bloom::Create(device, HdrFormat);
    m_fxaa = Fxaa::Create(device, m_format);
    // The shapes queued with DebugDraw, over the surfaces and hidden by them where they test depth.
    AddHook(RenderStage::AfterOpaque, [this](const RenderStageContext& stage) {
        if (DebugDraw::GetLines().empty()) {
            return;
        }
        auto pass = stage.context.BeginRenderPass(stage.pass);
        m_lines->Begin(pass, stage.viewProjection);
        m_lines->DrawDebugLines();
        m_lines->End();
    });
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
        sprites = sprites || scene->FindComponent<SpriteRenderer>() || scene->FindComponent<TextRenderer>() ||
                  scene->FindComponent<Tilemap>();
    }
    if (!camera && !scenes.empty() && !m_warnedNoCamera) {
        m_warnedNoCamera = true;
        Log::GetEngineLogger().Warning("The scene has no enabled Camera to be seen through; it shows from the origin");
    }
    const Camera3D view = camera ? camera->GetCamera3D() : Camera3D{};
    const bool pixelPerfect = view.orthographic && view.pixelPerfect.enabled && view.pixelPerfect.IsValid();

    // With several samples, each pass draws into the multisampled target and resolves it into the HDR image, keeping
    // the samples for the passes drawn over it.
    Texture* samples = m_samples.get();
    const RenderPassDesc hdrPass{.target = samples ? samples : m_hdr.get(),
                                 .depthTarget = m_depth.get(),
                                 .resolveTarget = samples ? m_hdr.get() : nullptr,
                                 .keepSamples = samples != nullptr};
    const Vector2 size{static_cast<float>(width), static_cast<float>(height)};
    RenderStageContext stage{.device = m_device,
                             .context = context,
                             .stage = RenderStage::BeforeOpaque,
                             .camera = view,
                             .viewProjection = view.GetViewProjection(size),
                             .viewSize = size,
                             .pass = hdrPass,
                             .colorFormat = HdrFormat,
                             .sampleCount = m_sampleCount,
                             .depth = m_depth.get(),
                             .sceneColor = m_hdr.get()};
    const auto runStage = [&](RenderStage at) {
        stage.stage = at;
        for (const Hook& hook : m_hooks) {
            if (hook.stage == at) {
                hook.hook(stage);
            }
        }
        for (Scene* scene : scenes) {
            scene->RunRenderHooks(stage);
        }
    };

    RenderPassDesc clear = hdrPass;
    clear.clearColor = camera ? camera->GetBackground() : m_background;
    clear.clearDepth = 1.0f;
    context.BeginRenderPass(clear).End();
    runStage(RenderStage::BeforeOpaque);

    m_renderer3D->Begin(context, hdrPass, view);
    for (Scene* scene : scenes) {
        m_renderer3D->DrawScene(*scene);
    }
    m_renderer3D->End();
    runStage(RenderStage::AfterOpaque);

    // Sprites go over the meshes, as 2D games draw them, without a depth target.
    if (sprites) {
        RenderPassDesc spriteDesc = hdrPass;
        spriteDesc.depthTarget = nullptr;
        auto spritePass = context.BeginRenderPass(spriteDesc);
        m_sprites->Begin(spritePass, view, size);
        for (Scene* scene : scenes) {
            m_sprites->DrawScene(*scene);
        }
        for (Scene* scene : scenes) {
            m_sprites->DrawWorldText(*scene, view);
        }
        m_sprites->End();
    }
    bool hasParticles = false;
    for (Scene* scene : scenes)
        scene->ForEachComponent<ParticleSystem>([&](ParticleSystem& system) {
            hasParticles = hasParticles || (system.IsActiveAndEnabled() && !system.GetParticles().empty());
        });
    if (hasParticles) {
        const Texture* particleDepth = m_sampleCount == 1 ? m_depth.get() : m_renderer3D->RenderCameraDepth(context);
        m_particles->Begin(context, hdrPass, view, size, particleDepth);
        for (Scene* scene : scenes)
            m_particles->DrawScene(*scene);
        m_particles->End();
    }
    runStage(RenderStage::AfterTransparent);
    runStage(RenderStage::BeforePostProcessing);

    const PostProcessing* look = PostProcessing::Find(scenes);
    TonemapSettings tonemapping = look ? look->GetTonemapping() : m_tonemapping;
    if (pixelPerfect)
        tonemapping.dither = false;
    const BloomSettings bloom = look ? look->GetBloom() : m_bloomSettings;
    const GradingSettings grading = look ? look->GetGrading() : m_grading;
    const Texture* glow = pixelPerfect ? nullptr : m_bloom->Render(context, *m_hdr, bloom);
    // FXAA reads the finished image, so it is tonemapped into a texture first.
    const bool fxaa = !pixelPerfect && m_fxaaEnabled && UpdateTonemapped(width, height);
    if (fxaa) {
        auto tonemapped = context.BeginRenderPass({.target = m_tonemapped.get()});
        m_tonemapper->Draw(tonemapped, *m_hdr, tonemapping, grading, glow, bloom.intensity);
    }
    {
        // It covers the whole target.
        auto output = context.BeginRenderPass({.target = target});
        if (fxaa) {
            m_fxaa->Draw(output, *m_tonemapped);
        } else {
            m_tonemapper->Draw(output, *m_hdr, tonemapping, grading, glow, bloom.intensity);
        }
    }

    // From here on, into the target in its colors.
    stage.pass = {.target = target};
    stage.colorFormat = m_format;
    stage.sampleCount = 1;
    stage.depth = nullptr;
    stage.sceneColor = nullptr;
    runStage(RenderStage::AfterPostProcessing);
    {
        auto interfacePass = context.BeginRenderPass({.target = target});
        m_interface->Begin(interfacePass, Scene::ScreenCamera(size), size);
        for (Scene* scene : scenes) {
            scene->DrawUi(*m_interface, size);
        }
        m_interface->End();
    }
    runStage(RenderStage::Overlay);
}

RenderHookId SceneRenderer::AddHook(RenderStage stage, RenderHook hook) {
    const auto id = static_cast<RenderHookId>(m_nextHook++);
    m_hooks.push_back({id, stage, std::move(hook)});
    return id;
}

void SceneRenderer::RemoveHook(RenderHookId id) {
    std::erase_if(m_hooks, [id](const Hook& hook) { return hook.id == id; });
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
