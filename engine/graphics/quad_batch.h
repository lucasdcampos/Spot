#pragma once

#include "graphics/color.h"
#include "math/vector.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Spot {

class Texture;

// A rectangle of a texture in UV space, from (0, 0) at its top-left to (1, 1) at its bottom-right. The default
// covers the whole texture.
struct TextureRegion {
    Vector2 min = {0.0f, 0.0f};
    Vector2 max = {1.0f, 1.0f};

    // The region of a sprite sheet cell or other sub-image, given in pixels from the texture's top-left.
    static TextureRegion FromPixels(const Texture& texture, float x, float y, float width, float height);
};

struct QuadVertex {
    Vector2 position;
    Vector2 uv;
    Color color;
};

// A run of consecutive quads that share a texture, drawn with a single draw call.
struct QuadDrawCommand {
    // Null for untextured quads.
    const Texture* texture = nullptr;
    std::uint32_t firstQuad = 0;
    std::uint32_t quadCount = 0;
};

// Collects quads as vertices plus the draw calls that render them, merging consecutive quads that use the same
// texture. It only builds CPU data, which Renderer2D sends to the GPU; custom renderers can use it as well.
class QuadBatch {
public:
    // Each quad has four vertices: top-left, top-right, bottom-right and bottom-left, mapped to the matching corners
    // of the texture region. Drawing them takes indices 0, 1, 2, 2, 3, 0 per quad.
    static constexpr std::uint32_t VerticesPerQuad = 4;
    static constexpr std::uint32_t IndicesPerQuad = 6;

    void Clear();
    // Rotation is in radians, counterclockwise around the center.
    void AddQuad(Vector2 center, Vector2 size, float rotation, const Texture* texture, Color color,
                 const TextureRegion& region = {});

    std::span<const QuadVertex> GetVertices() const { return m_vertices; }
    std::span<const QuadDrawCommand> GetCommands() const { return m_commands; }
    std::uint32_t GetQuadCount() const { return static_cast<std::uint32_t>(m_vertices.size() / VerticesPerQuad); }
    bool IsEmpty() const { return m_vertices.empty(); }

private:
    std::vector<QuadVertex> m_vertices;
    std::vector<QuadDrawCommand> m_commands;
};

} // namespace Spot
