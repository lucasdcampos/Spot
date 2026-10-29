#pragma once

#include "spot/render/camera_2d.h"
#include "spot/scene/entity.h"
#include "spot/scene/scene_system.h"
#include "spot/scene/ui/ui_layout.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Spot {

class Entity;
class Canvas;
class Renderer2D;

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
    // Draws active UiDrawListeners after canvas layout, restoring the full viewport scissor.
    void DrawUi(Renderer2D& renderer, Vector2 viewSize);
    // Maps one world unit to one pixel, with (0, 0) at the bottom-left of the view.
    static Camera2D ScreenCamera(Vector2 viewSize);
    std::span<const UiLayoutEntry> GetEntries() const { return m_entries; }
    const UiLayoutEntry* Find(EntityId entity) const;
    Vector2 GetViewSize() const { return m_viewSize; }

private:
    Vector2 Measure(Entity& entity);
    Vector2 GetPreferredSize(EntityId entity) const;
    std::vector<std::pair<EntityId, Vector2>> m_preferred;
    void LayoutChildren(Entity& parent, EntityId canvas, UiBounds bounds, UiBounds clip, float scale);
    Vector2 m_viewSize;
    std::vector<UiLayoutEntry> m_entries;
    struct CanvasEntry {
        EntityId id;
        int order;
        std::size_t index;
    };
    std::vector<CanvasEntry> m_canvases;
    // The draw listeners outside every canvas, in the order the scene gives them. Finding them goes through every
    // component of the scene, so they are found again only when what it holds changed.
    struct Listener {
        EntityId entity;
        ComponentId component;
    };
    std::vector<Listener> m_listeners;
    std::optional<std::uint64_t> m_listenersVersion;
};

} // namespace Spot
