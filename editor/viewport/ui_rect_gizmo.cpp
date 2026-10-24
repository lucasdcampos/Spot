#include "viewport/ui_rect_gizmo.h"
#include "spot/scene/ui/ui_system.h"

#include "scene/scene_commands.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace Spot::Editor {
namespace {
UiBounds ParentBounds(const Entity& entity, const UiSystem& ui, float scale) {
    for (const auto* parent = entity.GetParent(); parent; parent = parent->GetParent()) {
        if (const auto* entry = ui.Find(parent->GetId())) {
            auto bounds = entry->bounds;
            if (const auto* scroll = parent->GetComponent<UiScrollView>(); scroll && scroll->IsActiveAndEnabled())
                bounds = scroll->GetContentBounds(bounds, scale);
            return {bounds.min / scale, bounds.max / scale};
        }
    }
    return {{}, ui.GetViewSize() / scale};
}
bool IsManaged(const Entity& entity) {
    const auto* parent = entity.GetParent();
    const auto* group = parent ? parent->GetComponent<UiLayoutGroup>() : nullptr;
    return group && group->IsActiveAndEnabled();
}
} // namespace

UiRectLayout ApplyUiAnchorPreset(UiRectLayout layout, UiBounds parent, UiBounds current, Vector2 anchorMin,
                                 Vector2 anchorMax, Vector2 pivot, bool keepRectangle) {
    layout.anchorMin = anchorMin;
    layout.anchorMax = anchorMax;
    layout.pivot = pivot;
    if (!keepRectangle) {
        layout.offset = {};
        layout.sizeDelta = {anchorMin.x == anchorMax.x ? current.GetSize().x : 0.0f,
                            anchorMin.y == anchorMax.y ? current.GetSize().y : 0.0f};
        return layout;
    }
    const Vector2 parentSize = parent.GetSize();
    const Vector2 stretch{parentSize.x * (anchorMax.x - anchorMin.x), parentSize.y * (anchorMax.y - anchorMin.y)};
    layout.sizeDelta = current.GetSize() - stretch;
    const Vector2 position{current.min.x + current.GetSize().x * pivot.x,
                           current.min.y + current.GetSize().y * pivot.y};
    const Vector2 anchor{parent.min.x + parentSize.x * anchorMin.x + stretch.x * pivot.x,
                         parent.min.y + parentSize.y * anchorMin.y + stretch.y * pivot.y};
    layout.offset = position - anchor;
    return layout;
}
bool SetUiAnchorPreset(SceneDocument& document, EntityId id, Vector2 viewSize, Vector2 anchorMin, Vector2 anchorMax,
                       Vector2 pivot, bool keepRectangle) {
    auto* entity = document.GetScene().GetEntity(id);
    auto* rect = entity ? entity->GetComponent<UiRect>() : nullptr;
    if (!rect || IsManaged(*entity) || entity->GetComponent<Canvas>())
        return false;
    auto& ui = document.GetScene().GetOrAddSystem<Spot::UiSystem>();
    ui.Layout(viewSize);
    const auto* entry = ui.Find(id);
    if (!entry)
        return false;
    const auto before = SaveComponentToString(*rect);
    rect->layout = ApplyUiAnchorPreset(rect->layout, ParentBounds(*entity, ui, entry->scale),
                                       {entry->bounds.min / entry->scale, entry->bounds.max / entry->scale}, anchorMin,
                                       anchorMax, pivot, keepRectangle);
    const auto after = SaveComponentToString(*rect);
    return document.Execute(
        std::make_unique<EditComponentCommand>(document, id, entity->GetComponentIndex(*rect), before, after));
}
UiRect* UiRectGizmo::Resolve(SceneDocument& document) const {
    if (document.GetId() != m_document || document.GetSceneRevision() != m_revision)
        return nullptr;
    auto* entity = document.GetScene().GetEntity(m_entity);
    auto* rect = entity ? entity->GetComponent<UiRect>() : nullptr;
    return rect && rect->GetId() == m_component ? rect : nullptr;
}
bool UiRectGizmo::Begin(SceneDocument& document, EntityId entity, Vector2 viewSize) {
    if (m_dragging)
        Finish(document);
    auto* target = document.GetScene().GetEntity(entity);
    auto* rect = target ? target->GetComponent<UiRect>() : nullptr;
    if (!rect || IsManaged(*target) || target->GetComponent<Canvas>())
        return false;
    auto& ui = document.GetScene().GetOrAddSystem<Spot::UiSystem>();
    ui.Layout(viewSize);
    const auto* entry = ui.Find(entity);
    if (!entry)
        return false;
    m_entity = entity;
    m_component = rect->GetId();
    m_document = document.GetId();
    m_revision = document.GetSceneRevision();
    m_start = rect->layout;
    m_size = entry->bounds.GetSize() / entry->scale;
    m_scale = entry->scale;
    m_before = SaveComponentToString(*rect);
    m_dragging = true;
    return true;
}
bool UiRectGizmo::Translate(SceneDocument& document, Vector2 delta) {
    auto* rect = Resolve(document);
    if (!m_dragging || !rect)
        return false;
    rect->layout.offset = m_start.offset + delta / m_scale;
    return true;
}
bool UiRectGizmo::Resize(SceneDocument& document, Vector2 delta, Vector2 corner) {
    auto* rect = Resolve(document);
    if (!m_dragging || !rect)
        return false;
    Vector2 size{std::max(1.0f, m_size.x + delta.x * corner.x / m_scale),
                 std::max(1.0f, m_size.y + delta.y * corner.y / m_scale)};
    if (const auto* fitter = rect->GetEntity().GetComponent<UiSizeFitter>(); fitter && fitter->IsActiveAndEnabled()) {
        if (fitter->fitWidth)
            size.x = m_size.x;
        if (fitter->fitHeight)
            size.y = m_size.y;
    }
    const Vector2 change = size - m_size;
    rect->layout.sizeDelta = m_start.sizeDelta + change;
    rect->layout.offset = m_start.offset + Vector2{change.x * (m_start.pivot.x - (corner.x < 0 ? 1.0f : 0.0f)),
                                                   change.y * (m_start.pivot.y - (corner.y < 0 ? 1.0f : 0.0f))};
    return true;
}
bool UiRectGizmo::Finish(SceneDocument& document) {
    auto* rect = Resolve(document);
    const bool dragging = std::exchange(m_dragging, false);
    if (!dragging || !rect)
        return false;
    return document.Execute(std::make_unique<EditComponentCommand>(
        document, m_entity, rect->GetEntity().GetComponentIndex(*rect), m_before, SaveComponentToString(*rect)));
}
void UiRectGizmo::Cancel(SceneDocument& document) {
    if (auto* rect = Resolve(document); m_dragging && rect)
        LoadComponentFromString(*rect, m_before, document.GetAssetManager());
    m_dragging = false;
}

bool UiRectGizmo::Draw(SceneDocument& document, ImVec2 origin, ImVec2 size, Vector2 pixels) {
    if (pixels.x <= 0 || pixels.y <= 0)
        return false;
    auto& ui = document.GetScene().GetOrAddSystem<Spot::UiSystem>();
    ui.Layout(pixels);
    const auto toScreen = [&](Vector2 point) {
        return ImVec2{origin.x + point.x / pixels.x * size.x, origin.y + (1 - point.y / pixels.y) * size.y};
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    for (const auto& entry : ui.GetEntries())
        if (entry.entity == entry.canvas)
            draw->AddRect(toScreen({entry.bounds.min.x, entry.bounds.max.y}),
                          toScreen({entry.bounds.max.x, entry.bounds.min.y}), IM_COL32(140, 150, 165, 130));
    const auto id = document.GetSelection().GetPrimary();
    const auto* entry = ui.Find(id);
    auto* entity = document.GetScene().GetEntity(id);
    const auto* rect = entity ? entity->GetComponent<UiRect>() : nullptr;
    if (!entry || !rect) {
        draw->PopClipRect();
        if (m_dragging)
            Finish(document);
        return false;
    }
    const ImVec2 min = toScreen({entry->bounds.min.x, entry->bounds.max.y});
    const ImVec2 max = toScreen({entry->bounds.max.x, entry->bounds.min.y});
    const ImU32 color = IM_COL32(80, 180, 255, 255);
    draw->AddRect(min, max, color, 0.0f, 2.0f, ImDrawFlags_None);
    const auto parent = ParentBounds(*entity, ui, entry->scale);
    for (const auto anchor : {rect->layout.anchorMin, rect->layout.anchorMax}) {
        const auto point = toScreen(
            (parent.min + Vector2{parent.GetSize().x * anchor.x, parent.GetSize().y * anchor.y}) * entry->scale);
        draw->AddCircle(point, 5, IM_COL32(255, 210, 80, 255), 12, 2);
    }
    const auto pivot = toScreen(entry->bounds.min + Vector2{entry->bounds.GetSize().x * rect->layout.pivot.x,
                                                            entry->bounds.GetSize().y * rect->layout.pivot.y});
    draw->AddCircleFilled(pivot, 3, color);
    const std::array corners{Vector2{-1, -1}, Vector2{1, -1}, Vector2{1, 1}, Vector2{-1, 1}};
    Vector2 hoveredCorner{};
    const auto& io = ImGui::GetIO();
    const float handle = 5 * ImGui::GetStyle().FontScaleDpi;
    for (const auto corner : corners) {
        const ImVec2 point = toScreen({corner.x < 0 ? entry->bounds.min.x : entry->bounds.max.x,
                                       corner.y < 0 ? entry->bounds.min.y : entry->bounds.max.y});
        draw->AddRectFilled({point.x - handle, point.y - handle}, {point.x + handle, point.y + handle}, color);
        if (std::abs(io.MousePos.x - point.x) <= handle + 2 && std::abs(io.MousePos.y - point.y) <= handle + 2)
            hoveredCorner = corner;
    }
    draw->PopClipRect();
    const bool inside =
        io.MousePos.x >= min.x && io.MousePos.x <= max.x && io.MousePos.y >= min.y && io.MousePos.y <= max.y;
    const bool belowToolbar =
        io.MousePos.y > origin.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2;
    const bool hovered = belowToolbar && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         (inside || hoveredCorner != Vector2{});
    if (!m_dragging && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !io.KeyCtrl &&
        Begin(document, id, pixels)) {
        m_mouseStart = {io.MousePos.x, io.MousePos.y};
        m_corner = hoveredCorner;
    }
    if (m_dragging) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            Cancel(document);
            return true;
        }
        if (!Resolve(document)) {
            m_dragging = false;
            return false;
        }
        const Vector2 delta{(io.MousePos.x - m_mouseStart.x) * pixels.x / size.x,
                            -(io.MousePos.y - m_mouseStart.y) * pixels.y / size.y};
        if (m_corner == Vector2{})
            Translate(document, delta);
        else
            Resize(document, delta, m_corner);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            Finish(document);
        return true;
    }
    if (hovered && hoveredCorner != Vector2{})
        ImGui::SetMouseCursor(hoveredCorner.x == hoveredCorner.y ? ImGuiMouseCursor_ResizeNESW
                                                                 : ImGuiMouseCursor_ResizeNWSE);
    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}
void UiRectGizmo::DrawAnchorMenu(SceneDocument& document, Vector2 viewSize) {
    const auto id = document.GetSelection().GetPrimary();
    const auto* entity = document.GetScene().GetEntity(id);
    if (!entity || !entity->GetComponent<UiRect>())
        return;
    ImGui::SameLine();
    if (ImGui::Button("Anchors"))
        ImGui::OpenPopup("##UiAnchorPresets");
    if (ImGui::BeginPopup("##UiAnchorPresets")) {
        ImGui::Checkbox("Keep rectangle", &m_keepRectangle);
        static constexpr std::array<const char*, 9> Names{
            "Top Left", "Top", "Top Right", "Left", "Center", "Right", "Bottom Left", "Bottom", "Bottom Right"};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) {
                if (column)
                    ImGui::SameLine();
                if (ImGui::Button(Names[static_cast<std::size_t>(row * 3 + column)])) {
                    const Vector2 anchor{static_cast<float>(column) * 0.5f, 1 - static_cast<float>(row) * 0.5f};
                    SetUiAnchorPreset(document, id, viewSize, anchor, anchor, anchor, m_keepRectangle);
                    ImGui::CloseCurrentPopup();
                }
            }
        if (ImGui::Button("Stretch")) {
            SetUiAnchorPreset(document, id, viewSize, {}, {1, 1}, {0.5f, 0.5f}, m_keepRectangle);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace Spot::Editor
