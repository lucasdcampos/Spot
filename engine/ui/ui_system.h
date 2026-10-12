#pragma once

#include "scene/entity.h"
#include "scene/scene_system.h"
#include "ui/ui_layout.h"

#include <span>
#include <vector>

namespace Spot {

class Entity;
class Canvas;

struct UiLayoutEntry {
    EntityId entity = EntityId::Invalid;
    EntityId canvas = EntityId::Invalid;
    UiBounds bounds;
    UiBounds clip;
    float scale = 1.0f;
};

// Layout is transient and holds ids, so scene edits and game reloads cannot leave dangling pointers.
class UiSystem final : public SceneSystem {
public:
    void Layout(Vector2 viewSize);
    std::span<const UiLayoutEntry> GetEntries() const { return m_entries; }
    const UiLayoutEntry* Find(EntityId entity) const;
    Vector2 GetViewSize() const { return m_viewSize; }

private:
    void LayoutChildren(Entity& parent, EntityId canvas, UiBounds bounds, UiBounds clip, float scale);
    Vector2 m_viewSize;
    std::vector<UiLayoutEntry> m_entries;
    std::vector<EntityId> m_canvases;
};

} // namespace Spot
