#pragma once

#include <spot.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace Spot::Editor {

// A mesh placed in the world, as the editor draws it to pick entities and outline the selection.
struct MeshInstance {
    const Mesh* mesh = nullptr;
    Matrix4 world = {};
    std::uint32_t submesh = Mesh::AllSubmeshes;
};

// The textures one view of a scene is drawn into: the color shown in the window, the HDR image the scene is drawn into
// first, with its depth and multisampled image, and a mask of where the selection is.
class ViewportTargets {
public:
    // Recreates the textures when the size or sample count changed. Returns false, after logging why, when they
    // cannot be created.
    bool Resize(GraphicsDevice& device, std::uint32_t width, std::uint32_t height, std::uint32_t sampleCount);

    // Null until the first Resize().
    Texture* GetColor() const { return m_color.get(); }
    Texture* GetHdr() const { return m_hdr.get(); }
    // The tonemapped image FXAA reads.
    Texture* GetTonemapped() const { return m_tonemapped.get(); }
    // The multisampled HDR image; null with one sample.
    Texture* GetSamples() const { return m_samples.get(); }
    Texture* GetDepth() const { return m_depth.get(); }
    Texture* GetSelectionMask() const { return m_mask.get(); }
    std::uint32_t GetWidth() const { return m_width; }
    std::uint32_t GetHeight() const { return m_height; }

private:
    std::unique_ptr<Texture> m_color;
    std::unique_ptr<Texture> m_hdr;
    std::unique_ptr<Texture> m_tonemapped;
    std::unique_ptr<Texture> m_samples;
    std::unique_ptr<Texture> m_depth;
    std::unique_ptr<Texture> m_mask;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::uint32_t m_sampleCount = 0;
};

struct ViewportOptions {
    bool showGrid = true;
    // The grid lies on the XY plane, as 2D views see it, instead of the ground.
    bool grid2D = false;
    // What shows where nothing is drawn; the editor's dark gray when unset.
    std::optional<Color> background = std::nullopt;
    // Scenes without lights get the editor's, so what is in them shows; games see them dark, as they are.
    bool defaultLights = true;
    // The scene's components draw their interfaces over it, as in the game; see Scene::DrawUi().
    bool drawUi = false;
    // The colliders, character controllers and joints of the scene, as physics sees them.
    bool showColliders = false;
    // Lines only this view draws, besides those queued with DebugDraw.
    std::span<const DebugLine> lines = {};
    // How the HDR image of the scene becomes the view's colors, where the scene has no PostProcessing.
    TonemapSettings tonemapping = {};
    GradingSettings grading = {};
    BloomSettings bloom = {};
    bool fxaa = false;
};

// Draws scenes into viewport targets, as they would look in the game, with the shapes queued with DebugDraw and the
// editor's grid over them: as light into an HDR image, which a Tonemapper turns into the view's colors, as games'
// SceneRenderer does; then the scene's interfaces over it. One is shared by every view.
class ViewportRenderer {
public:
    // The format viewport colors are drawn in: sRGB, as the renderers draw linear colors.
    static constexpr TextureFormat ColorFormat = TextureFormat::Rgba8Srgb;
    // The format scenes are drawn into before the tonemapper.
    static constexpr TextureFormat HdrFormat = TextureFormat::Rgba16Float;

    explicit ViewportRenderer(GraphicsDevice& device);
    ~ViewportRenderer();

    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;

    GraphicsDevice& GetDevice() { return m_device; }
    std::uint32_t GetSampleCount() const { return m_sampleCount; }

    // Draws the scene seen through the camera into the targets, which must be sized for this renderer.
    void Render(RenderContext& context, const ViewportTargets& targets, Scene& scene, const Camera3D& camera,
                const ViewportOptions& options);
    // Draws a line of the color around where the meshes cover the view, over what Render() drew, reaching out the
    // width in pixels, up to 3.
    void DrawOutline(RenderContext& context, const ViewportTargets& targets, const Camera3D& camera,
                     std::span<const MeshInstance> meshes, Color color, float width);

    // Draws which of the meshes covers the pixel, counted from the top-left corner of a view of the size, for
    // ReadPick() to tell in a later frame.
    void RenderPick(RenderContext& context, const Camera3D& camera, Vector2 viewSize, Vector2 pixel,
                    std::span<const MeshInstance> meshes);
    // The index of the mesh that covered the pixel when RenderPick() last drew, once the GPU has drawn it; nothing
    // when no mesh did, or when nothing was drawn since the last call.
    std::optional<std::size_t> ReadPick();

private:
    void DrawGrid(RenderContext& context, const ViewportTargets& targets, const Camera3D& camera, bool plane2D);

    GraphicsDevice& m_device;
    std::uint32_t m_sampleCount;
    std::unique_ptr<Renderer3D> m_renderer;
    std::unique_ptr<Renderer2D> m_sprites;
    // Interfaces are drawn after the tonemapper, in the view's colors.
    std::unique_ptr<Renderer2D> m_interface;
    std::unique_ptr<LineRenderer> m_lines;
    std::unique_ptr<Tonemapper> m_tonemapper;
    std::unique_ptr<Bloom> m_bloom;
    std::unique_ptr<Fxaa> m_fxaa;
    std::unique_ptr<Pipeline> m_grid;
    std::unique_ptr<Pipeline> m_mask;
    std::unique_ptr<Pipeline> m_outline;
    std::unique_ptr<Pipeline> m_pick;
    std::unique_ptr<Sampler> m_maskSampler;
    // One pixel: picking draws only the pixel under the mouse, so reading it back is cheap.
    std::unique_ptr<Texture> m_pickColor;
    std::unique_ptr<Texture> m_pickDepth;
    bool m_pickDrawn = false;
};

} // namespace Spot::Editor
