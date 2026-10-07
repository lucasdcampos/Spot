#include "viewport/transform_gizmo.h"

#include "scene/entity_actions.h"
#include "scene/scene_document.h"

#include <imgui_internal.h>

#include <ImGuizmo.h>

#include <array>
#include <memory>
#include <utility>

namespace Spot::Editor {

namespace {

Vector3 TransformPoint(const Matrix4& matrix, Vector3 point) {
    const Vector4 result = matrix * Vector4{point.x, point.y, point.z, 1.0f};
    return {result.x, result.y, result.z};
}

float GetRatio(float from, float to) {
    return from != 0.0f ? to / from : 1.0f;
}

const char* GetStepName(GizmoTool tool) {
    switch (tool) {
    case GizmoTool::Rotate:
        return "Rotate";
    case GizmoTool::Scale:
        return "Scale";
    default:
        return "Move";
    }
}

} // namespace

Pose ApplyGizmoChange(GizmoTool tool, const GizmoTarget& target, const Matrix4& start, const Matrix4& current) {
    Pose pose = target.local;
    if (tool == GizmoTool::Scale) {
        const Vector3 from = Decompose(start).scale;
        const Vector3 to = Decompose(current).scale;
        pose.scale = {target.local.scale.x * GetRatio(from.x, to.x), target.local.scale.y * GetRatio(from.y, to.y),
                      target.local.scale.z * GetRatio(from.z, to.z)};
        return pose;
    }
    // What the gizmo did since the drag began, in the world, around its pivot. Moving and turning keep the gizmo's
    // scale, so this has none.
    const Matrix4 change = current * Inverse(start);
    pose.position = TransformPoint(Inverse(target.parentWorld), TransformPoint(change, target.worldPosition));
    if (tool == GizmoTool::Rotate) {
        const Quaternion turn = Decompose(current).rotation * Inverse(Decompose(start).rotation);
        pose.rotation = Normalize(Inverse(target.parentWorldRotation) * turn * target.worldRotation);
    }
    return pose;
}

void TransformGizmo::Draw(SceneDocument& document, const Camera3D& camera, ImVec2 origin, ImVec2 size,
                          const GizmoSettings& settings, int id, ImGuiID area) {
    Scene& scene = document.GetScene();
    const Entity* primary = scene.GetEntity(document.GetSelection().GetPrimary());
    const GizmoTool tool = m_dragging ? m_tool : settings.tool;
    if (!primary || tool == GizmoTool::Select) {
        if (m_dragging) {
            Finish(document);
        }
        return;
    }

    const Matrix4 view = camera.GetView();
    const Matrix4 projection = camera.GetProjection({size.x, size.y});
    Matrix4 matrix = primary->GetTransform().GetWorldMatrix();
    // In steps, unless Ctrl says otherwise for this drag.
    const bool snap = settings.snap != ImGui::GetIO().KeyCtrl;
    const float step = tool == GizmoTool::Move     ? settings.moveStep
                       : tool == GizmoTool::Rotate ? settings.rotateStep
                                                   : settings.scaleStep;
    const std::array steps{step, step, step};
    const ImGuizmo::OPERATION operation = tool == GizmoTool::Move     ? ImGuizmo::TRANSLATE
                                          : tool == GizmoTool::Rotate ? ImGuizmo::ROTATE
                                                                      : ImGuizmo::SCALE;

    // ImGuizmo only starts a drag while no item is hovered or active, but the view's area always is under the
    // handles; it is hidden from ImGuizmo meanwhile.
    ImGuiContext& imgui = *ImGui::GetCurrentContext();
    const auto hide = [&](ImGuiID& item) { return std::exchange(item, item == area ? 0 : item); };
    const ImGuiID hovered = hide(imgui.HoveredId);
    const ImGuiID hoveredBefore = hide(imgui.HoveredIdPreviousFrame);
    const ImGuiID active = hide(imgui.ActiveId);

    ImGuizmo::PushID(id);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    const bool changed = ImGuizmo::Manipulate(view.GetData(), projection.GetData(), operation,
                                              settings.local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &matrix.columns[0].x,
                                              nullptr, snap ? steps.data() : nullptr);
    const bool held = ImGuizmo::IsUsing();
    ImGuizmo::PopID();

    imgui.HoveredId = hovered;
    imgui.HoveredIdPreviousFrame = hoveredBefore;
    imgui.ActiveId = active;

    if (held && !m_dragging) {
        Begin(document, *primary, tool);
    }
    if (!m_dragging) {
        return;
    }
    if (!held) {
        Finish(document);
        return;
    }
    ImGui::KeepAliveID(m_activeId);
    if (changed) {
        for (const Dragged& dragged : m_dragged) {
            if (Entity* entity = scene.GetEntity(dragged.entity)) {
                ApplyGizmoChange(m_tool, dragged.target, m_start, matrix).ApplyTo(entity->GetTransform());
            }
        }
    }
}

bool TransformGizmo::WantsMouse() {
    return ImGuizmo::IsOver() || ImGuizmo::IsUsingAny();
}

void TransformGizmo::Begin(SceneDocument& document, const Entity& primary, GizmoTool tool) {
    m_dragging = true;
    m_tool = tool;
    m_start = primary.GetTransform().GetWorldMatrix();
    m_dragged.clear();
    // Entities below other selected ones move with them.
    for (const Entity* entity : GetSelectedEntities(document)) {
        const Transform& transform = entity->GetTransform();
        const Entity* parent = entity->GetParent();
        m_dragged.push_back({entity->GetId(),
                             {Pose::Of(transform), transform.GetWorldPosition(), transform.GetWorldRotation(),
                              parent ? parent->GetTransform().GetWorldMatrix() : Matrix4::Identity(),
                              parent ? parent->GetTransform().GetWorldRotation() : Quaternion::Identity()}});
    }
    // ImGui knows the mouse is taken while the handles are dragged, so the window does not move along, for one.
    m_activeId = ImGui::GetID("##TransformGizmo");
    ImGui::SetActiveID(m_activeId, ImGui::GetCurrentWindow());
}

void TransformGizmo::Finish(SceneDocument& document) {
    m_dragging = false;
    if (ImGui::GetActiveID() == m_activeId) {
        ImGui::ClearActiveID();
    }
    std::vector<SetTransformsCommand::Change> changes;
    for (const Dragged& dragged : std::exchange(m_dragged, {})) {
        if (const Entity* entity = document.GetScene().GetEntity(dragged.entity)) {
            changes.push_back({dragged.entity, dragged.target.local, Pose::Of(entity->GetTransform())});
        }
    }
    if (!changes.empty()) {
        document.Execute(std::make_unique<SetTransformsCommand>(document, GetStepName(m_tool), std::move(changes)));
    }
}

} // namespace Spot::Editor
