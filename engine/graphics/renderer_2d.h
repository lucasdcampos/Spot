#pragma once

#include "graphics/camera_2d.h"
#include "graphics/color.h"
#include "graphics/quad_batch.h"
#include "graphics/render_context.h"
#include "graphics/text_layout.h"
#include "graphics/texture.h"
#include "math/vector.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace Spot {

struct Camera3D;
class Buffer;
class Font;
class GraphicsDevice;
class Pipeline;
class RenderPass;
class Scene;
class SpriteRenderer;
class Tilemap;

struct Renderer2DConfig {
    // Nearest keeps pixel art sharp; linear smooths scaled textures.
    TextureFilter filter = TextureFilter::Linear;
    // The format of the textures it draws into; the window's swapchain when unset.
    std::optional<TextureFormat> targetFormat = std::nullopt;
    // The sample count of the targets of the passes it draws in; see TextureDesc::sampleCount.
    std::uint32_t sampleCount = 1;
};

struct Renderer2DStats {
    std::uint32_t quadCount = 0;
    std::uint32_t drawCalls = 0;
};

// Draws colored and textured quads, batched into as few draw calls as possible. It is built only on the public
// graphics API, and draws inside a render pass the caller opens, so it mixes freely with custom rendering.
//
//     renderer.Begin(pass, camera, viewSize);
//     renderer.DrawQuad({0, 0}, {100, 100}, Spot::Color::Red);
//     renderer.End();
//
// Quads are drawn in the order given, later ones on top, with alpha blending. Textures must stay alive until End().
class Renderer2D {
public:
    explicit Renderer2D(GraphicsDevice& device, const Renderer2DConfig& config = {});
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // Whether its GPU resources were created; when not, drawing does nothing and the reason has been logged.
    bool IsValid() const;

    void Begin(RenderPass& pass, const Camera2D& camera, Vector2 viewSize);
    // Draws in the XY plane of the world as the matrix sees it, such as a Camera3D's view projection, so sprites can
    // show in 3D views too.
    void Begin(RenderPass& pass, const Matrix4& viewProjection);
    void End();

    // Positions are the quad's center, in world units.
    void DrawQuad(Vector2 position, Vector2 size, Color color);
    // The region picks part of the texture, such as one frame of a sprite sheet.
    void DrawQuad(Vector2 position, Vector2 size, const Texture& texture, Color tint = Color::White,
                  const TextureRegion& region = {});
    // Rotation is in radians, counterclockwise.
    void DrawRotatedQuad(Vector2 position, Vector2 size, float rotation, Color color);
    void DrawRotatedQuad(Vector2 position, Vector2 size, float rotation, const Texture& texture,
                         Color tint = Color::White, const TextureRegion& region = {});

    // Draws every SpriteRenderer that is enabled on an active entity, at its entity's world position, rotation around
    // Z and scale, in their order, and of the same order the lower ones along Z first. The scene must not change
    // until End(), since its sprites keep their textures alive.
    void DrawScene(Scene& scene);
    // Draws world text over sprites, ordered by its order and entity traversal.
    void DrawWorldText(Scene& scene, const Camera3D& camera);

    // Draws UTF-8 text with its top-left corner at the position. At scale 1 a pixel of the font covers one world
    // unit, which is one window point with the default camera. Fonts must stay alive until End().
    void DrawString(const Font& font, std::string_view text, Vector2 position, Color color = Color::White,
                    float scale = 1.0f);

    void DrawGlyphs(const Font& font, std::span<const GlyphQuad> glyphs, Vector2 position, Color color = Color::White,
                    float scale = 1);
    // Flushes pending quads before changing the pass's clip rectangle.
    void SetScissor(const ScissorRect& rect);

    // What the last End() drew.
    Renderer2DStats GetStats() const { return m_stats; }

private:
    void Flush();
    bool EnsureCapacity(std::uint32_t quadCount);

    GraphicsDevice& m_device;
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Sampler> m_sampler;
    std::unique_ptr<Texture> m_whiteTexture;
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    std::uint32_t m_capacity = 0;

    QuadBatch m_batch;
    RenderPass* m_pass = nullptr;
    Matrix4 m_viewProjection;
    bool m_worldDepth = false;
    std::optional<ScissorRect> m_scissor;
    Renderer2DStats m_stats;
    struct SceneDraw {
        const SpriteRenderer* sprite = nullptr;
        const Tilemap* tilemap = nullptr;
        Vector3 position = {};
        float rotation = 0;
        Vector2 size = {};
        int order = 0;
        std::size_t index = 0;
    };
    std::vector<SceneDraw> m_sceneDraws;
};

} // namespace Spot
