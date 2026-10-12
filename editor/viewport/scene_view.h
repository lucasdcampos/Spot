#pragma once

#include "viewport/editor_camera.h"
#include "viewport/transform_gizmo.h"
#include "viewport/ui_rect_gizmo.h"
#include "viewport/viewport_renderer.h"

#include <spot.h>

#include <imgui.h>

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Spot::Editor {

class EditorContext;
class SceneDocument;

// A window that shows a scene document through the editor camera, which the mouse and keys move around. Clicking
// selects what is under the mouse, and the transform gizmo moves the selection.
class SceneView {
public:
    explicit SceneView(SceneDocument& document);
    ~SceneView();

    SceneView(const SceneView&) = delete;
    SceneView& operator=(const SceneView&) = delete;

    SceneDocument& GetDocument() { return m_document; }
    // What the view shows: its document, or the copy of it being played.
    SceneDocument& GetShownDocument() { return *m_shown; }
    // Shows the document again once it stops playing, right away: the copy goes away, maybe after the view was drawn
    // and before it renders.
    void StopShowingPlay();
    // While its document plays, whether the view looks through the game's main camera, as the game would show,
    // rather than the editor's.
    bool IsShowingGame() const { return m_playing && m_gameView; }
    EditorCamera& GetCamera() { return m_camera; }
    ViewportOptions& GetOptions() { return m_options; }
    // The id ImGui knows the window by, which stays the same across runs for scenes saved as assets.
    const std::string& GetWindowId() const { return m_windowId; }
    // Brings the window to the front on the next frame.
    void RequestFocus() { m_focusRequested = true; }

    struct DrawResult {
        // The window has focus, so its document is the one being edited.
        bool focused = false;
        // The user asked to close it.
        bool closeRequested = false;
    };
    // New windows dock into the given node. Without a renderer, as in tests, the view stays empty.
    DrawResult Draw(EditorContext& context, ViewportRenderer* renderer, ImGuiID dockId);
    // Draws the scene into the view's targets, if the window showed this frame.
    void Render(RenderContext& context, ViewportRenderer& renderer);

    // Frames the selection, or the whole scene when nothing is selected.
    void FrameSelection();

    // Where the entity's icon, such as a light's, was drawn last frame, in screen coordinates, as {min, max};
    // nothing when it has none or it was not in view.
    std::optional<std::pair<ImVec2, ImVec2>> GetIconRect(EntityId entity) const;

private:
    // A click to select what is under the mouse.
    struct PickRequest {
        // In the view's pixels, from its top-left corner.
        Vector2 pixel;
        // Ctrl adds the entity to the selection, or takes it out.
        bool toggle = false;
        // Shift adds the entity to the selection.
        bool add = false;
        // A material dropped on the view, which the entity under the mouse gets instead of being selected.
        AssetId material = {};
    };

    struct Icon {
        EntityId entity;
        ImVec2 min;
        ImVec2 max;
    };

    void DrawViewport(EditorContext& context, ViewportRenderer* renderer);
    void UpdateCamera(EditorContext& context, bool hovered, bool active, float viewHeight);
    void DrawIcons(const ImVec2& origin, const ImVec2& size);
    // Lines around what cameras see, for placing them.
    void DrawCameraFrustums(const ImVec2& origin, const ImVec2& size);
    // The meshes the view draws for an entity and those below it, sprites included, for picking and outlines.
    void AddMeshes(const Entity& entity, std::vector<MeshInstance>& meshes) const;
    void DrawViewCube(const ImVec2& origin, const ImVec2& size);
    void DrawOverlay(const ImVec2& origin);
    void DrawSpeedButton();
    void HandleClick(const ImVec2& origin, const ImVec2& scale);
    void HandleAssetDrop(EditorContext& context, std::span<const AssetId> assets, const ImVec2& origin,
                         const ImVec2& size, const ImVec2& scale);
    // Where the mouse points on the ground, the plane through the origin facing up, or the camera's pivot when it
    // points at the sky or too far away.
    Vector3 GetGroundPoint(const ImVec2& origin, const ImVec2& size) const;
    void ApplyPick(const PickRequest& request, EntityId entity);
    int GetGizmoId() const;

    SceneDocument& m_document;
    // Kept for the view's life, so saving a new scene does not make it another window.
    std::string m_windowId;
    bool m_focusRequested = false;
    EditorCamera m_camera;
    ViewportOptions m_options;
    // The project's look and quality, which views use where their scene has no PostProcessing.
    RenderingSettings m_rendering;
    // Kept from frame to frame, so drawing the colliders allocates nothing once warmed up.
    DebugLines m_colliderLines;
    ViewportTargets m_targets;
    std::unique_ptr<SceneRenderer> m_gameRenderer;
    std::uint64_t m_gameRenderingRevision = 0;
    // Pixels of the view this frame; 0 when the window is hidden.
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    bool m_looking = false;
    SceneDocument* m_shown;
    bool m_playing = false;
    bool m_gameView = true;
    TransformGizmo m_gizmo;
    UiRectGizmo m_uiRectGizmo;
    // The icons drawn last, which clicks select before meshes.
    std::vector<Icon> m_icons;
    // A click waiting to be drawn into the picking target, and one drawn, waiting to be read back, with the entity of
    // each mesh it drew.
    std::optional<PickRequest> m_pick;
    std::optional<PickRequest> m_picking;
    std::vector<EntityId> m_pickEntities;
    // Sprites are picked and outlined as this plane, turned to face +Z.
    AssetRef<Mesh> m_plane;
    float m_outlineWidth = 2.0f;
    Vector2 m_viewOrigin;
};

} // namespace Spot::Editor
