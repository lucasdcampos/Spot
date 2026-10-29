#pragma once

#include "spot/core/math/color.h"
#include "spot/graphics/render_graph.h"
#include "spot/graphics/texture.h"
#include "spot/render/ambient_occlusion.h"
#include "spot/render/bloom.h"
#include "spot/render/fxaa.h"
#include "spot/render/render_stage.h"
#include "spot/render/tonemapper.h"

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
    bool gpuCulling = false;
    bool depthPrepass = true;
    // See Renderer3DConfig; games draw scenes with these on.
    AmbientOcclusionSettings ambientOcclusion = {.enabled = true};
    bool volumetricClouds = true;
    std::uint32_t skyOcclusionSize = 512;
    float skyOcclusionDistance = 64.0f;
};

// Draws a scene through its first main camera: meshes, debug shapes, sprites and particles into HDR,
// post-processing, then interfaces. Built on the public graphics API, into a window or texture.
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

    // Draws the scene into the target, or the window when null. Runs its hooks before the scene hooks at each stage.
    void Render(RenderContext& context, Scene& scene, Texture* target = nullptr);

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
