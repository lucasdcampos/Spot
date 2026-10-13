#pragma once
#include "scene/scene_document.h"
#include <imgui.h>
#include <optional>
#include <spot.h>

namespace Spot::Editor {
class TilemapPainter {
public:
    bool Begin(SceneDocument& document, EntityId entity, int tile);
    bool Paint(SceneDocument& document, int x, int y);
    bool Finish(SceneDocument& document);
    void Cancel(SceneDocument& document);
    bool IsPainting() const { return m_painting; }
    void Draw(SceneDocument& document, EntityId entity);
    std::optional<std::pair<ImVec2, ImVec2>> GetGridRect() const { return m_grid; }

private:
    Tilemap* Resolve(SceneDocument& document) const;
    EntityId m_entity = EntityId::Invalid;
    ComponentId m_component;
    std::uint64_t m_document = 0;
    std::uint64_t m_revision = 0;
    std::uint64_t m_history = 0;
    std::string m_before;
    bool m_painting = false;
    int m_tile = 0;
    int m_brush = 0;
    std::optional<std::pair<int, int>> m_last;
    std::optional<std::pair<ImVec2, ImVec2>> m_grid;
};
} // namespace Spot::Editor
