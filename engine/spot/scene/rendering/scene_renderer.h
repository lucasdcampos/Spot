#pragma once

#include "spot/graphics/bloom.h"
#include "spot/graphics/fxaa.h"
#include "spot/graphics/render_graph.h"
#include "spot/graphics/render_stage.h"
#include "spot/graphics/texture.h"
#include "spot/graphics/tonemapper.h"
#include "spot/math/color.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Spot {

class GraphicsDevice;
class LineRenderer;
class RenderContext;
class Renderer2D;
class Renderer3D;
class ParticleRenderer;
class Scene;

struct SceneRendererConfig {
    // The format of the textures it draws into; the window's swapchain when unset.
    std::optional<TextureFormat> targetFormat = std::nullopt;
    // Samples per pixel for multisampled antialiasing, used where the GPU supports them, and 1 otherwise.
    std::uint32_t sampleCount = 4;
    // What shows where nothing is drawn when the scenes have no camera.
    Color background = {0.12f, 0.13f, 0.15f, 1.0f};
    // How the HDR image of the scenes becomes what the target shows.
    TonemapSettings tonemapping = {};
    GradingSettings grading = {};
    BloomSettings bloom = {};
    // Smooths the steps of edges after tonemapping, next to multisampling or instead of it; see Fxaa.
    bool fxaa = false;
    // See Renderer3DConfig.
    std::uint32_t shadowMapSize = 2048;
    std::uint32_t shadowCascadeCount = 4;
    float shadowDistance = 60.0f;
    std::uint32_t localShadowMapSize = 512;
};

// Draws scenes as games show them: through the first main camera among them (see FindMainCamera()), their meshes,
// then the shapes queued with DebugDraw, then their sprites over the meshes, all as light into an HDR image, which a
// Tonemapper turns into what the target shows; then their interfaces over everything, in the target's colors. The
// look comes from the first active PostProcessing among the scenes, or from the renderer's settings without one.
// Applications draw their scenes with one by default (see ApplicationConfig::renderScenes); it is built only on the
// public graphics API, so games that draw their own way can use it too, into the window or into a texture.
class SceneRenderer {
public:
    explicit SceneRenderer(GraphicsDevice& device, const SceneRendererConfig& config = {});
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // False when its GPU resources could not be created, after logging why; it then draws nothing.
    bool IsValid() const;
    std::uint32_t GetSampleCount() const { return m_sampleCount; }
    const TonemapSettings& GetTonemapping() const { return m_tonemapping; }
    void SetTonemapping(const TonemapSettings& tonemapping) { m_tonemapping = tonemapping; }
    const GradingSettings& GetGrading() const { return m_grading; }
    void SetGrading(const GradingSettings& grading) { m_grading = grading; }
    const BloomSettings& GetBloom() const { return m_bloomSettings; }
    void SetBloom(const BloomSettings& bloom) { m_bloomSettings = bloom; }
    bool IsFxaaEnabled() const { return m_fxaaEnabled; }
    void SetFxaaEnabled(bool enabled) { m_fxaaEnabled = enabled; }

    // Draws the scenes, in order, over each other, into the target: a render target in the configured format, or the
    // window when null. At each RenderStage it runs its own hooks, then those of the scenes (see
    // Scene::AddRenderHook()).
    void Render(RenderContext& context, std::span<Scene* const> scenes, Texture* target = nullptr);

    // Draws at the stage of every frame this renderer draws, whatever the scenes, after the hooks added before it, as
    // the shapes queued with DebugDraw are drawn after the opaque stage.
    RenderHookId AddHook(RenderStage stage, RenderHook hook);
    void RemoveHook(RenderHookId id);

private:
    GraphicsDevice& m_device;
    TextureFormat m_format;
    std::uint32_t m_sampleCount;
    Color m_background;
    TonemapSettings m_tonemapping;
    GradingSettings m_grading;
    BloomSettings m_bloomSettings;
    bool m_fxaaEnabled;
    std::unique_ptr<Renderer3D> m_renderer3D;
    // Sprites are drawn into the HDR image with the meshes, interfaces into the target after it.
    std::unique_ptr<ParticleRenderer> m_particles;
    std::unique_ptr<Renderer2D> m_sprites;
    std::unique_ptr<Renderer2D> m_interface;
    std::unique_ptr<LineRenderer> m_lines;
    std::unique_ptr<Tonemapper> m_tonemapper;
    std::unique_ptr<Bloom> m_bloom;
    std::unique_ptr<Fxaa> m_fxaa;
    // Describes each frame, and keeps its HDR, depth and other targets from frame to frame, at the frame's size.
    RenderGraph m_graph;
    bool m_warnedNoCamera = false;
    struct Hook {
        RenderHookId id;
        RenderStage stage;
        RenderHook hook;
    };
    std::vector<Hook> m_hooks;
    std::uint32_t m_nextHook = 1;
};

} // namespace Spot
