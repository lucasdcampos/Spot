#include "ui/ui_system.h"

#include "core/profiler.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "ui/canvas.h"

#include <algorithm>
#include <cmath>

namespace Spot {

const UiLayoutEntry* UiSystem::Find(EntityId entity) const {
    const auto found = std::ranges::find(m_entries, entity, &UiLayoutEntry::entity);
    return found == m_entries.end() ? nullptr : &*found;
}

void UiSystem::Layout(Vector2 viewSize) {
    SPOT_PROFILE_SCOPE("UiSystem::Layout");
    m_viewSize = viewSize;
    m_entries.clear();
    m_canvases.clear();
    if (!std::isfinite(viewSize.x) || !std::isfinite(viewSize.y) || viewSize.x <= 0 || viewSize.y <= 0)
        return;
    GetScene().ForEachComponent<Canvas>([&](Canvas& canvas) {
        if (!canvas.IsFaulted())
            m_canvases.push_back(canvas.GetEntity().GetId());
    });
    std::stable_sort(m_canvases.begin(), m_canvases.end(), [&](EntityId a, EntityId b) {
        return GetScene().GetEntity(a)->GetComponent<Canvas>()->settings.order <
               GetScene().GetEntity(b)->GetComponent<Canvas>()->settings.order;
    });
    for (const auto id : m_canvases) {
        auto& entity = *GetScene().GetEntity(id);
        const auto& settings = entity.GetComponent<Canvas>()->settings;
        const float scale = ComputeUiScale(settings, viewSize);
        const UiBounds viewport{{}, viewSize};
        m_entries.push_back({id, id, viewport, viewport, scale});
        LayoutChildren(entity, id, viewport, viewport, scale);
    }
}

void UiSystem::LayoutChildren(Entity& parent, EntityId canvas, UiBounds bounds, UiBounds clip, float scale) {
    for (auto* child : parent.GetChildren()) {
        if (!child->IsActiveInHierarchy() || child->GetComponent<Canvas>())
            continue;
        UiBounds childBounds = bounds;
        if (const auto* rect = child->GetComponent<UiRect>(); rect && rect->IsEnabled() && !rect->IsFaulted()) {
            childBounds = ComputeUiRect(rect->layout, {bounds.min / scale, bounds.max / scale});
            childBounds = {childBounds.min * scale, childBounds.max * scale};
            m_entries.push_back({child->GetId(), canvas, childBounds, clip, scale});
        }
        LayoutChildren(*child, canvas, childBounds, clip, scale);
    }
}

} // namespace Spot
