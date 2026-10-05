#include "graphics/quad_batch.h"

#include <array>
#include <cmath>

namespace Spot {

void QuadBatch::Clear() {
    m_vertices.clear();
    m_commands.clear();
}

void QuadBatch::AddQuad(Vector2 center, Vector2 size, float rotation, const Texture* texture, Color color) {
    const Vector2 half = size / 2.0f;
    const std::array<Vector2, 4> corners{{{-half.x, half.y}, {half.x, half.y}, {half.x, -half.y}, {-half.x, -half.y}}};
    const std::array<Vector2, 4> uvs{{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};

    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Vector2 corner = corners[i];
        const Vector2 rotated{corner.x * cosine - corner.y * sine, corner.x * sine + corner.y * cosine};
        m_vertices.push_back({center + rotated, uvs[i], color});
    }

    if (!m_commands.empty() && m_commands.back().texture == texture) {
        ++m_commands.back().quadCount;
    } else {
        m_commands.push_back({texture, GetQuadCount() - 1, 1});
    }
}

} // namespace Spot
