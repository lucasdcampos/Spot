#pragma once

#include "graphics/bloom.h"
#include "graphics/color.h"
#include "graphics/fxaa.h"
#include "graphics/texture.h"
#include "graphics/tonemapper.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace Spot {

class GraphicsDevice;
class LineRenderer;
class RenderContext;
class Renderer2D;
class Renderer3D;
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
};

// Draws scenes as games show them: through the first main camera among them (see FindMainCamera()), their meshes,
// then the shapes queued with DebugDraw, then their sprites over the meshes, all as light into an HDR image, which a
// Tonemapper turns into what the target shows; then their interfaces over everything, in the target's colors.
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
    // window when null.
    void Render(RenderContext& context, std::span<Scene* const> scenes, Texture* target = nullptr);

private:
    // The HDR, depth and multisampled targets must match the frame's size, which changes with the window.
    bool UpdateTargets(std::uint32_t width, std::uint32_t height);
    bool UpdateTonemapped(std::uint32_t width, std::uint32_t height);

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
    std::unique_ptr<Renderer2D> m_sprites;
    std::unique_ptr<Renderer2D> m_interface;
    std::unique_ptr<LineRenderer> m_lines;
    std::unique_ptr<Tonemapper> m_tonemapper;
    std::unique_ptr<Bloom> m_bloom;
    std::unique_ptr<Fxaa> m_fxaa;
    // The tonemapped image FXAA reads, in the target's format; made when FXAA is first on.
    std::unique_ptr<Texture> m_tonemapped;
    std::unique_ptr<Texture> m_hdr;
    std::unique_ptr<Texture> m_depth;
    std::unique_ptr<Texture> m_samples;
    bool m_warnedNoCamera = false;
};

} // namespace Spot
