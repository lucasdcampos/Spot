#include "inspector/tilemap_painter.h"
#include "imgui/imgui_renderer.h"
#include "scene/scene_commands.h"
#include <algorithm>
#include <cmath>

namespace Spot::Editor {
Tilemap* TilemapPainter::Resolve(SceneDocument& document) const {
    if (document.GetId() != m_document || document.GetSceneRevision() != m_revision ||
        document.GetHistory().GetCurrentState() != m_history)
        return nullptr;
    auto* entity = document.GetScene().GetEntity(m_entity);
    auto* map = entity ? entity->GetComponent<Tilemap>() : nullptr;
    return map && map->GetId() == m_component ? map : nullptr;
}
bool TilemapPainter::Begin(SceneDocument& document, EntityId id, int tile) {
    if (m_painting)
        Finish(document);
    auto* entity = document.GetScene().GetEntity(id);
    auto* map = entity ? entity->GetComponent<Tilemap>() : nullptr;
    if (!map || tile < -1 || (tile >= 0 && static_cast<std::size_t>(tile) >= map->GetData().definitions.size()))
        return false;
    m_document = document.GetId();
    m_revision = document.GetSceneRevision();
    m_history = document.GetHistory().GetCurrentState();
    m_entity = id;
    m_component = map->GetId();
    m_tile = tile;
    m_before = SaveComponentToString(*map);
    m_last.reset();
    m_painting = true;
    return true;
}
bool TilemapPainter::Paint(SceneDocument& document, int x, int y) {
    if (!m_painting)
        return false;
    auto* map = Resolve(document);
    if (!map) {
        m_painting = false;
        return false;
    }
    if (x < 0 || y < 0 || x >= map->GetData().width || y >= map->GetData().height)
        return false;
    auto [fromX, fromY] = m_last.value_or(std::pair{x, y});
    const int dx = std::abs(x - fromX), dy = -std::abs(y - fromY), sx = fromX < x ? 1 : -1, sy = fromY < y ? 1 : -1;
    int error = dx + dy;
    bool changed = false;
    while (true) {
        changed |= map->SetCell(fromX, fromY, m_tile);
        if (fromX == x && fromY == y)
            break;
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            fromX += sx;
        }
        if (twice <= dx) {
            error += dx;
            fromY += sy;
        }
    }
    m_last = std::pair{x, y};
    return changed;
}
bool TilemapPainter::Finish(SceneDocument& document) {
    if (!m_painting)
        return false;
    m_painting = false;
    auto* map = Resolve(document);
    if (!map)
        return false;
    const auto after = SaveComponentToString(*map);
    return document.Execute(std::make_unique<EditComponentCommand>(
        document, m_entity, map->GetEntity().GetComponentIndex(*map), m_before, after));
}
void TilemapPainter::Cancel(SceneDocument& document) {
    if (m_painting)
        if (auto* map = Resolve(document))
            LoadComponentFromString(*map, m_before);
    m_painting = false;
    m_last.reset();
}
void TilemapPainter::Draw(SceneDocument& document, EntityId id) {
    auto* entity = document.GetScene().GetEntity(id);
    auto* map = entity ? entity->GetComponent<Tilemap>() : nullptr;
    if (!map)
        return;
    if (m_painting && (id != m_entity || !Resolve(document))) {
        Finish(document);
    }
    const auto& data = map->GetData();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderInt("Brush", &m_brush, -1, std::max(-1, static_cast<int>(data.definitions.size()) - 1));
    ImGui::TextDisabled("Left drag paints. Right drag erases. Escape cancels.");
    const float cell = 24;
    if (ImGui::BeginChild("TileGrid", {0, 300}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        const auto origin = ImGui::GetCursorScreenPos();
        const ImVec2 size{static_cast<float>(data.width) * cell, static_cast<float>(data.height) * cell};
        ImGui::InvisibleButton("Grid", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        m_grid = std::pair{origin, ImVec2{origin.x + size.x, origin.y + size.y}};
        auto* draw = ImGui::GetWindowDrawList();
        const auto* atlas = map->GetAtlas().Get();
        const auto* texture = atlas ? atlas->texture.Get() : nullptr;
        const auto clipMin = ImGui::GetWindowPos();
        const auto clipSize = ImGui::GetWindowSize();
        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x) {
                const ImVec2 min{origin.x + static_cast<float>(x) * cell,
                                 origin.y + static_cast<float>(data.height - 1 - y) * cell};
                const ImVec2 max{min.x + cell, min.y + cell};
                if (max.x < clipMin.x || max.y < clipMin.y || min.x > clipMin.x + clipSize.x ||
                    min.y > clipMin.y + clipSize.y)
                    continue;
                const int tile = map->GetCell(x, y);
                if (tile >= 0 && static_cast<std::size_t>(tile) < data.definitions.size()) {
                    const auto& definition = data.definitions[static_cast<std::size_t>(tile)];
                    if (texture) {
                        const auto region = atlas->GetRegion(definition.sprite);
                        draw->AddImage(ToImTextureId(*texture), min, max, {region.min.x, region.min.y},
                                       {region.max.x, region.max.y});
                    } else
                        draw->AddRectFilled(
                            min, max, definition.solid ? IM_COL32(100, 165, 210, 255) : IM_COL32(130, 180, 120, 255));
                }
                draw->AddRect(min, max, IM_COL32(100, 100, 100, 140));
            }
        if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)))
            Begin(document, id, ImGui::IsMouseDown(1) ? -1 : m_brush);
        if (m_painting && ImGui::IsItemActive()) {
            const auto mouse = ImGui::GetIO().MousePos;
            Paint(document, static_cast<int>(std::floor((mouse.x - origin.x) / cell)),
                  data.height - 1 - static_cast<int>(std::floor((mouse.y - origin.y) / cell)));
        }
    }
    ImGui::EndChild();
    if (m_painting && ImGui::IsKeyPressed(ImGuiKey_Escape))
        Cancel(document);
    else if (m_painting && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1))
        Finish(document);
}
} // namespace Spot::Editor
