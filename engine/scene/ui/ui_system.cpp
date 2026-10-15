#include "scene/ui/ui_system.h"

#include "core/profiler.h"
#include "graphics/texture.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/ui/canvas.h"
#include "scene/ui/ui_layout_group.h"
#include "scene/ui/ui_widgets.h"

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
    m_preferred.clear();
    if (!std::isfinite(viewSize.x) || !std::isfinite(viewSize.y) || viewSize.x <= 0 || viewSize.y <= 0)
        return;
    GetScene().ForEachComponent<Canvas>([&](Canvas& canvas) {
        if (!canvas.IsFaulted())
            m_canvases.push_back({canvas.GetEntity().GetId(), canvas.settings.order, m_canvases.size()});
    });
    std::sort(m_canvases.begin(), m_canvases.end(),
              [](const auto& a, const auto& b) { return a.order != b.order ? a.order < b.order : a.index < b.index; });
    for (const auto& canvas : m_canvases)
        Measure(*GetScene().GetEntity(canvas.id));
    for (const auto& canvas : m_canvases) {
        const auto id = canvas.id;
        auto& entity = *GetScene().GetEntity(id);
        const auto& settings = entity.GetComponent<Canvas>()->settings;
        const float scale = ComputeUiScale(settings, viewSize);
        const UiBounds viewport{{}, viewSize};
        m_entries.push_back({id, id, viewport, viewport, scale});
        LayoutChildren(entity, id, viewport, viewport, scale);
    }
}

Vector2 UiSystem::GetPreferredSize(EntityId entity) const {
    const auto found = std::ranges::find_if(m_preferred, [&](const auto& item) { return item.first == entity; });
    return found == m_preferred.end() ? Vector2{} : found->second;
}

Vector2 UiSystem::Measure(Entity& entity) {
    auto* group = entity.GetComponent<UiLayoutGroup>();
    if (group && group->IsActiveAndEnabled()) {
        group->m_children.clear();
        group->m_sizes.clear();
    } else
        group = nullptr;
    entity.ForEachChild([&](Entity& child) {
        if (!child.IsActiveInHierarchy() || child.GetComponent<Canvas>())
            return;
        const auto size = Measure(child);
        if (group) {
            const auto* rect = child.GetComponent<UiRect>();
            if (rect && rect->IsActiveAndEnabled()) {
                group->m_children.push_back(child.GetId());
                group->m_sizes.push_back(size);
            }
        }
    });
    const auto* rect = entity.GetComponent<UiRect>();
    Vector2 size = rect ? rect->layout.sizeDelta : Vector2{};
    if (const auto* fitter = entity.GetComponent<UiSizeFitter>(); fitter && fitter->IsActiveAndEnabled()) {
        Vector2 preferred = size;
        if (group)
            preferred = MeasureUiLayoutGroup(group->settings, group->m_sizes);
        else if (const auto* text = entity.GetComponent<UiText>(); text && text->IsActiveAndEnabled())
            preferred = text->GetPreferredSize();
        else if (const auto* image = entity.GetComponent<UiImage>();
                 image && image->IsActiveAndEnabled() && image->texture) {
            preferred = {static_cast<float>(image->texture->GetWidth()),
                         static_cast<float>(image->texture->GetHeight())};
        }
        if (fitter->fitWidth)
            size.x = preferred.x;
        if (fitter->fitHeight)
            size.y = preferred.y;
    }
    size = {std::isfinite(size.x) ? std::max(0.0f, size.x) : 0, std::isfinite(size.y) ? std::max(0.0f, size.y) : 0};
    m_preferred.emplace_back(entity.GetId(), size);
    return size;
}

void UiSystem::LayoutChildren(Entity& parent, EntityId canvas, UiBounds bounds, UiBounds clip, float scale) {
    auto* group = parent.GetComponent<UiLayoutGroup>();
    if (group && group->IsActiveAndEnabled()) {
        ArrangeUiLayoutGroup(group->settings, {bounds.min / scale, bounds.max / scale}, group->m_sizes,
                             group->m_bounds);
    } else
        group = nullptr;
    parent.ForEachChild([&](Entity& childEntity) {
        auto* child = &childEntity;
        if (!child->IsActiveInHierarchy() || child->GetComponent<Canvas>())
            return;
        UiBounds childBounds = bounds;
        if (const auto* rect = child->GetComponent<UiRect>(); rect && rect->IsEnabled() && !rect->IsFaulted()) {
            auto layout = rect->layout;
            const auto parentBounds = UiBounds{bounds.min / scale, bounds.max / scale};
            childBounds = ComputeUiRect(layout, parentBounds);
            if (const auto* fitter = child->GetComponent<UiSizeFitter>(); fitter && fitter->IsActiveAndEnabled()) {
                const auto preferred = GetPreferredSize(child->GetId());
                if (fitter->fitWidth)
                    layout.sizeDelta.x += preferred.x - childBounds.GetSize().x;
                if (fitter->fitHeight)
                    layout.sizeDelta.y += preferred.y - childBounds.GetSize().y;
                childBounds = ComputeUiRect(layout, parentBounds);
            }
            if (group) {
                const auto found = std::ranges::find(group->m_children, child->GetId());
                if (found != group->m_children.end())
                    childBounds = group->m_bounds[static_cast<std::size_t>(found - group->m_children.begin())];
            }
            childBounds = {childBounds.min * scale, childBounds.max * scale};
            m_entries.push_back({child->GetId(), canvas, childBounds, clip, scale});
        }
        UiBounds childClip = clip;
        if (auto* scroll = child->GetComponent<UiScrollView>(); scroll && scroll->IsActiveAndEnabled()) {
            scroll->SetOffset(scroll->GetOffset());
            childClip = IntersectUiBounds(clip, childBounds);
            childBounds = scroll->GetContentBounds(childBounds, scale);
        }
        LayoutChildren(*child, canvas, childBounds, childClip, scale);
    });
}

} // namespace Spot
