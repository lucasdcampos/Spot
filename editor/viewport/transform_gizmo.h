#pragma once

#include "core/editor_settings.h"
#include "scene/scene_commands.h"

#include <spot.h>

#include <imgui.h>

#include <vector>

namespace Spot::Editor {

class SceneDocument;

// An entity as it was when a drag of the gizmo began.
struct GizmoTarget {
    Pose local;
    Vector3 worldPosition;
    Quaternion worldRotation;
    // The parent's, or the identity for entities at the root.
    Matrix4 parentWorld;
    Quaternion parentWorldRotation;
};

// The local pose the entity gets when the gizmo's matrix goes from start to current: moved and turned with it, around
// the gizmo's pivot, or scaled as much as the gizmo was along its own axes.
Pose ApplyGizmoChange(GizmoTool tool, const GizmoTarget& target, const Matrix4& start, const Matrix4& current);

// Handles over a scene view that move, turn and scale the selected entities around the primary one's pivot. Each drag
// is one undoable step.
class TransformGizmo {
public:
    // Draws the handles for the document's selection over the view's rectangle in the current window, and changes
    // the entities while they are dragged. The id tells the gizmos of different views apart. The area is the item
    // that takes the view's mouse input, under the handles: the gizmo starts a drag over it as if it were not there.
    void Draw(SceneDocument& document, const Camera3D& camera, ImVec2 origin, ImVec2 size,
              const GizmoSettings& settings, int id, ImGuiID area);
    bool IsDragging() const { return m_dragging; }

    // Whether the mouse was over the handles of a gizmo last frame, or one is being dragged, so views leave the mouse
    // to them.
    static bool WantsMouse();

private:
    struct Dragged {
        EntityId entity;
        GizmoTarget target;
    };

    void Begin(SceneDocument& document, const Entity& primary, GizmoTool tool);
    void Finish(SceneDocument& document);

    std::vector<Dragged> m_dragged;
    Matrix4 m_start;
    GizmoTool m_tool = GizmoTool::Move;
    bool m_dragging = false;
    ImGuiID m_activeId = 0;
};

} // namespace Spot::Editor
