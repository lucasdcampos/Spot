#pragma once

#include <spot.h>

#include <cstdint>
#include <memory>

namespace Spot::Editor {

// The textures one view of a scene is drawn into: the color shown in the window, and the depth and the multisampled
// color it is drawn with.
class ViewportTargets {
public:
    // Recreates the textures when the size or sample count changed. Returns false, after logging why, when they
    // cannot be created.
    bool Resize(GraphicsDevice& device, std::uint32_t width, std::uint32_t height, std::uint32_t sampleCount);

    // Null until the first Resize().
    Texture* GetColor() const { return m_color.get(); }
    Texture* GetSamples() const { return m_samples.get(); }
    Texture* GetDepth() const { return m_depth.get(); }
    std::uint32_t GetWidth() const { return m_width; }
    std::uint32_t GetHeight() const { return m_height; }

private:
    std::unique_ptr<Texture> m_color;
    std::unique_ptr<Texture> m_samples;
    std::unique_ptr<Texture> m_depth;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::uint32_t m_sampleCount = 0;
};

struct ViewportOptions {
    bool showGrid = true;
};

// Draws scenes into viewport targets, as they would look in the game, with the editor's grid over them. One is shared
// by every view.
class ViewportRenderer {
public:
    // The format viewport colors are drawn in: sRGB, as the renderers draw linear colors.
    static constexpr TextureFormat ColorFormat = TextureFormat::Rgba8Srgb;

    explicit ViewportRenderer(GraphicsDevice& device);
    ~ViewportRenderer();

    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;

    GraphicsDevice& GetDevice() { return m_device; }
    std::uint32_t GetSampleCount() const { return m_sampleCount; }

    // Draws the scene seen through the camera into the targets, which must be sized for this renderer.
    void Render(RenderContext& context, const ViewportTargets& targets, Scene& scene, const Camera3D& camera,
                const ViewportOptions& options);

private:
    void DrawGrid(RenderContext& context, const ViewportTargets& targets, const Camera3D& camera);

    GraphicsDevice& m_device;
    std::uint32_t m_sampleCount;
    std::unique_ptr<Renderer3D> m_renderer;
    std::unique_ptr<Pipeline> m_grid;
};

} // namespace Spot::Editor
