#pragma once

#include "graphics/camera_2d.h"
#include "graphics/color.h"
#include "graphics/quad_batch.h"
#include "graphics/texture.h"
#include "math/vector.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace Spot {

class Buffer;
class GraphicsDevice;
class Pipeline;
class RenderPass;

struct Renderer2DConfig {
    // Nearest keeps pixel art sharp; linear smooths scaled textures.
    TextureFilter filter = TextureFilter::Linear;
    // The format of the textures it draws into; the window's swapchain when unset.
    std::optional<TextureFormat> targetFormat = std::nullopt;
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
    void End();

    // Positions are the quad's center, in world units.
    void DrawQuad(Vector2 position, Vector2 size, Color color);
    void DrawQuad(Vector2 position, Vector2 size, const Texture& texture, Color tint = Color::White);
    // Rotation is in radians, counterclockwise.
    void DrawRotatedQuad(Vector2 position, Vector2 size, float rotation, Color color);
    void DrawRotatedQuad(Vector2 position, Vector2 size, float rotation, const Texture& texture,
                         Color tint = Color::White);

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
    Renderer2DStats m_stats;
};

} // namespace Spot
