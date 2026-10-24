#include "spot/scene/rendering/scene_renderer.h"
#include "spot/scene/rendering/tilemap.h"

#include "spot/core/debug/profiler.h"

#include "spot/core/debug/log.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/render_context.h"
#include "spot/render/debug_draw.h"
#include "spot/render/line_renderer.h"
#include "spot/render/particle_renderer.h"
#include "spot/render/renderer_2d.h"
#include "spot/render/renderer_3d.h"
#include "spot/scene/particles/particle_system.h"
#include "spot/scene/rendering/camera.h"
#include "spot/scene/rendering/post_processing.h"
#include "spot/scene/rendering/scene_drawing.h"
#include "spot/scene/rendering/sprite_renderer.h"
#include "spot/scene/rendering/text_renderer.h"
#include "spot/scene/scene.h"

#include <array>

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

void SceneRenderer::Render(RenderContext& context, Scene& scene, Texture* target) {
    SPOT_PROFILE_SCOPE("SceneRenderer::Render");
    const auto width = static_cast<std::uint32_t>(target ? target->GetWidth() : context.GetWidth());
    const auto height = static_cast<std::uint32_t>(target ? target->GetHeight() : context.GetHeight());
    if (!IsValid() || width == 0 || height == 0) {
        return;
    }

    const Camera* camera = FindMainCamera(scene);
    const bool sprites =
        scene.FindComponent<SpriteRenderer>() || scene.FindComponent<TextRenderer>() || scene.FindComponent<Tilemap>();
    bool particles = false;
    scene.ForEachComponent<ParticleSystem>([&](ParticleSystem& system) {
        particles = particles || (system.IsActiveAndEnabled() && !system.GetParticles().empty());
    });
    if (!camera && !m_warnedNoCamera) {
        m_warnedNoCamera = true;
        Log::GetEngineLogger().Warning("The scene has no enabled Camera to be seen through; it shows from the origin");
    }
    const Camera3D view = camera ? camera->GetCamera3D() : Camera3D{};
    const bool pixelPerfect = view.orthographic && view.pixelPerfect.enabled && view.pixelPerfect.IsValid();

    const PostProcessing* look = PostProcessing::Find(std::array<Scene*, 1>{&scene});
    TonemapSettings tonemapping = look ? look->GetTonemapping() : m_tonemapping;
    if (pixelPerfect) {
        tonemapping.dither = false;
    }
    const BloomSettings bloom = look ? look->GetBloom() : m_bloomSettings;
    const GradingSettings grading = look ? look->GetGrading() : m_grading;
    // FXAA reads the finished image, so it is tonemapped into a texture first.
    const bool fxaa = !pixelPerfect && m_fxaaEnabled;

    const Vector2 size{static_cast<float>(width), static_cast<float>(height)};
    RenderStageContext stage{.device = m_device,
                             .context = context,
                             .stage = RenderStage::BeforeOpaque,
                             .camera = view,
                             .viewProjection = view.GetViewProjection(size),
                             .viewSize = size,
                             .pass = {},
                             .colorFormat = HdrFormat,
                             .sampleCount = m_sampleCount,
                             .depth = nullptr,
                             .sceneColor = nullptr};
    const auto runStage = [&](RenderStage at) {
        stage.stage = at;
        for (const Hook& hook : m_hooks) {
            if (hook.stage == at) {
                hook.hook(stage);
            }
        }
        scene.RunRenderHooks(stage);
    };

    const RenderGraphTexture output = m_graph.Import("Target", target);
    RenderGraphTexture hdr;
    RenderGraphTexture depth;
    RenderGraphTexture samples;
    // With several samples, each pass draws into the multisampled target and resolves it into the HDR image, keeping
    // the samples for the passes drawn over it.
    const auto hdrPass = [&](const RenderGraphResources& resources) {
        Texture* multisampled = samples.IsValid() ? resources.GetTexture(samples) : nullptr;
        const RenderPassDesc pass{.target = multisampled ? multisampled : resources.GetTexture(hdr),
                                  .depthTarget = resources.GetTexture(depth),
                                  .resolveTarget = multisampled ? resources.GetTexture(hdr) : nullptr,
                                  .keepSamples = multisampled != nullptr};
        stage.pass = pass;
        stage.depth = pass.depthTarget;
        stage.sceneColor = resources.GetTexture(hdr);
        return pass;
    };
    const auto drawsHdr = [&](RenderGraphBuilder& pass) {
        pass.Write(hdr);
        pass.Write(depth);
        if (samples.IsValid()) {
            pass.Write(samples);
        }
    };

    m_graph.AddPass(
        "Scene",
        [&](RenderGraphBuilder& pass) {
            hdr = pass.Create("HDR", {.width = width, .height = height, .format = HdrFormat, .renderTarget = true});
            depth = pass.Create("Depth", {.width = width,
                                          .height = height,
                                          .format = TextureFormat::Depth32Float,
                                          .renderTarget = true,
                                          .sampleCount = m_sampleCount});
            if (m_sampleCount > 1) {
                samples = pass.Create("Samples", {.width = width,
                                                  .height = height,
                                                  .format = HdrFormat,
                                                  .renderTarget = true,
                                                  .sampleCount = m_sampleCount});
            }
            drawsHdr(pass);
        },
        [&](RenderGraphResources& resources) {
            const RenderPassDesc pass = hdrPass(resources);
            RenderPassDesc clear = pass;
            clear.clearColor = camera ? camera->GetBackground() : m_background;
            clear.clearDepth = 1.0f;
            context.BeginRenderPass(clear).End();
            runStage(RenderStage::BeforeOpaque);

            m_renderer3D->Begin(context, pass, view);
            DrawScene(*m_renderer3D, scene);
            m_renderer3D->End();
            runStage(RenderStage::AfterOpaque);
        });

    // Sprites go over the meshes, as 2D games draw them, without a depth target.
    if (sprites) {
        m_graph.AddPass("Sprites", drawsHdr, [&](RenderGraphResources& resources) {
            RenderPassDesc spriteDesc = hdrPass(resources);
            spriteDesc.depthTarget = nullptr;
            auto spritePass = context.BeginRenderPass(spriteDesc);
            m_sprites->Begin(spritePass, view, size);
            DrawScene(*m_sprites, scene);
            DrawWorldText(*m_sprites, scene, view);
            m_sprites->End();
        });
    }
    if (particles) {
        m_graph.AddPass("Particles", drawsHdr, [&](RenderGraphResources& resources) {
            const RenderPassDesc pass = hdrPass(resources);
            const Texture* particleDepth =
                m_sampleCount == 1 ? pass.depthTarget : m_renderer3D->RenderCameraDepth(context);
            m_particles->Begin(context, pass, view, size, particleDepth);
            DrawScene(*m_particles, scene);
            m_particles->End();
        });
    }
    m_graph.AddPass("Transparent stages", drawsHdr, [&](RenderGraphResources& resources) {
        hdrPass(resources);
        runStage(RenderStage::AfterTransparent);
        runStage(RenderStage::BeforePostProcessing);
    });

    RenderGraphTexture tonemapped;
    m_graph.AddPass(
        "Post-processing",
        [&](RenderGraphBuilder& pass) {
            pass.Read(hdr);
            if (fxaa) {
                tonemapped = pass.Create("Tonemapped",
                                         {.width = width, .height = height, .format = m_format, .renderTarget = true});
                pass.Write(tonemapped);
            } else {
                pass.Write(output);
            }
        },
        [&](RenderGraphResources& resources) {
            const Texture& image = *resources.GetTexture(hdr);
            const Texture* glow = pixelPerfect ? nullptr : m_bloom->Render(context, image, bloom);
            // It covers the whole target.
            auto pass = context.BeginRenderPass({.target = resources.GetTexture(fxaa ? tonemapped : output)});
            m_tonemapper->Draw(pass, image, tonemapping, grading, glow, bloom.intensity);
        });
    if (fxaa) {
        m_graph.AddPass(
            "FXAA",
            [&](RenderGraphBuilder& pass) {
                pass.Read(tonemapped);
                pass.Write(output);
            },
            [&](RenderGraphResources& resources) {
                auto pass = context.BeginRenderPass({.target = resources.GetTexture(output)});
                m_fxaa->Draw(pass, *resources.GetTexture(tonemapped));
            });
    }

    m_graph.AddPass(
        "Interface", [&](RenderGraphBuilder& pass) { pass.Write(output); },
        [&](RenderGraphResources& resources) {
            // From here on, into the target in its colors.
            stage.pass = {.target = resources.GetTexture(output)};
            stage.colorFormat = m_format;
            stage.sampleCount = 1;
            stage.depth = nullptr;
            stage.sceneColor = nullptr;
            runStage(RenderStage::AfterPostProcessing);
            {
                auto interfacePass = context.BeginRenderPass(stage.pass);
                m_interface->Begin(interfacePass, Scene::ScreenCamera(size), size);
                scene.DrawUi(*m_interface, size);
                m_interface->End();
            }
            runStage(RenderStage::Overlay);
        });

    m_graph.Execute(m_device, context);
}

RenderHookId SceneRenderer::AddHook(RenderStage stage, RenderHook hook) {
    const auto id = static_cast<RenderHookId>(m_nextHook++);
    m_hooks.push_back({id, stage, std::move(hook)});
    return id;
}

void SceneRenderer::RemoveHook(RenderHookId id) {
    std::erase_if(m_hooks, [id](const Hook& hook) { return hook.id == id; });
}

} // namespace Spot
