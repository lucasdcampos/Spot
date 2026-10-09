#pragma once

#include "graphics/color.h"
#include "graphics/texture.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace Spot {

class GraphicsDevice;
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
};

// Draws scenes as games show them: through the first main camera among them (see FindMainCamera()), their meshes,
// then their sprites over the meshes, then their interfaces over everything. Applications draw their scenes with one
// by default (see ApplicationConfig::renderScenes); it is built only on the public graphics API, so games that draw
// their own way can use it too, into the window or into a texture.
class SceneRenderer {
public:
    explicit SceneRenderer(GraphicsDevice& device, const SceneRendererConfig& config = {});
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // False when its GPU resources could not be created, after logging why; it then draws nothing.
    bool IsValid() const;
    std::uint32_t GetSampleCount() const { return m_sampleCount; }

    // Draws the scenes, in order, over each other, into the target: a render target in the configured format, or the
    // window when null.
    void Render(RenderContext& context, std::span<Scene* const> scenes, Texture* target = nullptr);

private:
    // The depth and multisampled targets must match the frame's size, which changes with the window.
    bool UpdateTargets(std::uint32_t width, std::uint32_t height);

    GraphicsDevice& m_device;
    TextureFormat m_format;
    std::uint32_t m_sampleCount;
    Color m_background;
    std::unique_ptr<Renderer3D> m_renderer3D;
    std::unique_ptr<Renderer2D> m_renderer2D;
    std::unique_ptr<Texture> m_depth;
    std::unique_ptr<Texture> m_samples;
    bool m_warnedNoCamera = false;
};

} // namespace Spot
