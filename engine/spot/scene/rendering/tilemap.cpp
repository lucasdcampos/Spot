#include "spot/scene/rendering/tilemap.h"
#include "spot/assets/asset_archive.h"
#include "spot/core/archive.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Spot {
void TileDefinition::Serialize(Archive& archive) {
    archive.Field("sprite", sprite);
    archive.Field("solid", solid);
}
bool TilemapData::IsValid() const {
    if (width <= 0 || height <= 0 || width > MaxDimension || height > MaxDimension || !std::isfinite(cellSize.x) ||
        !std::isfinite(cellSize.y) || cellSize.x <= 0 || cellSize.y <= 0 ||
        cells.size() != static_cast<std::size_t>(width * height))
        return false;
    return std::ranges::all_of(definitions, [](const TileDefinition& tile) { return tile.sprite >= 0; }) &&
           std::ranges::all_of(cells, [&](int tile) {
               return tile == -1 || (tile >= 0 && static_cast<std::size_t>(tile) < definitions.size());
           });
}
int TilemapData::GetCell(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height)
        return -1;
    const auto index = static_cast<std::size_t>(y * width + x);
    return index < cells.size() ? cells[index] : -1;
}
bool TilemapData::SetCell(int x, int y, int tile) {
    if (x < 0 || y < 0 || x >= width || y >= height || tile < -1 ||
        (tile >= 0 && static_cast<std::size_t>(tile) >= definitions.size()))
        return false;
    const auto index = static_cast<std::size_t>(y * width + x);
    if (index >= cells.size() || cells[index] == tile)
        return false;
    cells[index] = tile;
    return true;
}
bool TilemapData::Resize(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0 || newWidth > MaxDimension || newHeight > MaxDimension)
        return false;
    if (newWidth == width && newHeight == height && cells.size() == static_cast<std::size_t>(width * height))
        return false;
    std::vector<int> resized(static_cast<std::size_t>(newWidth * newHeight), -1);
    for (int y = 0; y < std::min(height, newHeight); ++y)
        for (int x = 0; x < std::min(width, newWidth); ++x)
            resized[static_cast<std::size_t>(y * newWidth + x)] = GetCell(x, y);
    width = newWidth;
    height = newHeight;
    cells = std::move(resized);
    return true;
}
void TilemapData::BuildCollisionBoxes(std::vector<TileCollisionBox>& out) const {
    out.clear();
    if (!IsValid())
        return;
    std::array<int, MaxDimension> previous, current;
    previous.fill(-1);
    const auto solid = [&](int x, int y) {
        const int tile = GetCell(x, y);
        return tile >= 0 && definitions[static_cast<std::size_t>(tile)].solid;
    };
    for (int y = 0; y < height; ++y) {
        current.fill(-1);
        for (int x = 0; x < width;) {
            if (!solid(x, y)) {
                ++x;
                continue;
            }
            const int begin = x;
            while (x < width && solid(x, y))
                ++x;
            const float size = static_cast<float>(x - begin) * cellSize.x;
            const int prior = previous[static_cast<std::size_t>(begin)];
            if (prior >= 0 && out[static_cast<std::size_t>(prior)].size.x == size) {
                auto& box = out[static_cast<std::size_t>(prior)];
                box.size.y += cellSize.y;
                box.center.y += cellSize.y * 0.5f;
                current[static_cast<std::size_t>(begin)] = prior;
            } else {
                current[static_cast<std::size_t>(begin)] = static_cast<int>(out.size());
                out.push_back({{(static_cast<float>(begin) + static_cast<float>(x)) * 0.5f * cellSize.x,
                                (static_cast<float>(y) + 0.5f) * cellSize.y},
                               {size, cellSize.y}});
            }
        }
        previous = current;
    }
}
void TilemapData::Serialize(Archive& archive) {
    int newWidth = width, newHeight = height;
    archive.Field("width", newWidth, {.min = 1, .max = MaxDimension});
    archive.Field("height", newHeight, {.min = 1, .max = MaxDimension});
    if (archive.IsLoading())
        Resize(std::clamp(newWidth, 1, MaxDimension), std::clamp(newHeight, 1, MaxDimension));
    archive.Field("cellSize", cellSize, {.min = 0.001f, .max = 1000});
    archive.Field("definitions", definitions);
    archive.Field("cells", cells, {.hidden = true});
}
bool Tilemap::SetData(TilemapData data) {
    if (!data.IsValid())
        return false;
    m_data = std::move(data);
    ++m_revision;
    return true;
}
bool Tilemap::SetCell(int x, int y, int tile) {
    if (!m_data.SetCell(x, y, tile))
        return false;
    ++m_revision;
    return true;
}
bool Tilemap::Resize(int width, int height) {
    if (!m_data.Resize(width, height))
        return false;
    ++m_revision;
    return true;
}
void Tilemap::OnValidate() {
    m_data.width = std::clamp(m_data.width, 1, TilemapData::MaxDimension);
    m_data.height = std::clamp(m_data.height, 1, TilemapData::MaxDimension);
    m_data.cells.resize(static_cast<std::size_t>(m_data.width * m_data.height), -1);
    for (auto& tile : m_data.cells)
        if (tile < -1 || (tile >= 0 && static_cast<std::size_t>(tile) >= m_data.definitions.size()))
            tile = -1;
    if (!std::isfinite(m_data.cellSize.x) || m_data.cellSize.x <= 0)
        m_data.cellSize.x = 1;
    if (!std::isfinite(m_data.cellSize.y) || m_data.cellSize.y <= 0)
        m_data.cellSize.y = 1;
    for (auto& definition : m_data.definitions)
        definition.sprite = std::max(0, definition.sprite);
    ++m_revision;
}
void Tilemap::Serialize(Archive& archive) {
    archive.Field("atlas", m_atlas);
    archive.Field("data", m_data);
    archive.Field("order", m_order);
    archive.Field("color", m_color);
}
} // namespace Spot
