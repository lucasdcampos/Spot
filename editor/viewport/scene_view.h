#pragma once

#include "viewport/editor_camera.h"
#include "viewport/viewport_renderer.h"

#include <spot.h>

#include <imgui.h>

#include <string>

namespace Spot::Editor {

class EditorContext;
class SceneDocument;

// A window that shows a scene document through the editor camera, which the mouse and keys move around.
class SceneView {
public:
    explicit SceneView(SceneDocument& document);
    ~SceneView();

    SceneView(const SceneView&) = delete;
    SceneView& operator=(const SceneView&) = delete;

    SceneDocument& GetDocument() { return m_document; }
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

private:
    void DrawViewport(EditorContext& context, ViewportRenderer* renderer);
    void UpdateCamera(EditorContext& context, bool hovered, bool active, float viewHeight);
    void DrawIcons(const ImVec2& origin, const ImVec2& size);
    void DrawOverlay(const ImVec2& origin);

    SceneDocument& m_document;
    // Kept for the view's life, so saving a new scene does not make it another window.
    std::string m_windowId;
    bool m_focusRequested = false;
    EditorCamera m_camera;
    ViewportOptions m_options;
    ViewportTargets m_targets;
    // Pixels of the view this frame; 0 when the window is hidden.
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    bool m_looking = false;
};

} // namespace Spot::Editor
