#pragma once

#include "scene/scene_document.h"

#include <imgui.h>
#include <spot.h>

#include <string>

namespace Spot::Editor {

UiRectLayout ApplyUiAnchorPreset(UiRectLayout layout, UiBounds parent, UiBounds current, Vector2 anchorMin,
                                 Vector2 anchorMax, Vector2 pivot, bool keepRectangle = true);
bool SetUiAnchorPreset(SceneDocument& document, EntityId entity, Vector2 viewSize, Vector2 anchorMin, Vector2 anchorMax,
                       Vector2 pivot, bool keepRectangle = true);

// Drag state keeps ids and a component snapshot, never an entity pointer.
class UiRectGizmo {
public:
    bool Begin(SceneDocument& document, EntityId entity, Vector2 viewSize);
    bool Translate(SceneDocument& document, Vector2 pixelDelta);
    bool Resize(SceneDocument& document, Vector2 pixelDelta, Vector2 corner);
    bool Finish(SceneDocument& document);
    void Cancel(SceneDocument& document);
    bool IsDragging() const { return m_dragging; }
    // Draws canvas bounds and the selected rect's handles. Returns whether it owns this pointer interaction.
    bool Draw(SceneDocument& document, ImVec2 origin, ImVec2 size, Vector2 pixelSize);
    void DrawAnchorMenu(SceneDocument& document, Vector2 viewSize);

private:
    UiRect* Resolve(SceneDocument& document) const;
    bool m_dragging = false;
    EntityId m_entity = EntityId::Invalid;
    ComponentId m_component;
    std::uint64_t m_document = 0;
    std::uint64_t m_revision = 0;
    UiRectLayout m_start;
    Vector2 m_size;
    float m_scale = 1;
    std::string m_before;
    Vector2 m_mouseStart;
    Vector2 m_corner;
    bool m_keepRectangle = true;
};

} // namespace Spot::Editor
