#pragma once

#include "core/editor_settings.h"
#include "project/editor_project.h"
#include "scene/scene_document.h"

#include <spot.h>

#include <memory>

namespace Spot::Editor {

class SceneWorkspace;

// What the editor's windows share: the devices, the user's settings and the open project.
class EditorContext {
public:
    // Without a device, as in tests, nothing can be rendered or loaded to the GPU.
    EditorContext(Window& window, GraphicsDevice* device, EditorSettings& settings)
        : m_window(window), m_device(device), m_settings(settings) {}

    Window& GetWindow() { return m_window; }
    GraphicsDevice* GetGraphicsDevice() { return m_device; }
    EditorSettings& GetSettings() { return m_settings; }

    // Null while no project is open.
    EditorProject* GetProject() { return m_project.get(); }
    const EditorProject* GetProject() const { return m_project.get(); }
    void SetProject(std::unique_ptr<EditorProject> project) { m_project = std::move(project); }

    // The scenes open in the editor; null in tests that have none.
    SceneWorkspace* GetWorkspace() { return m_workspace; }
    void SetWorkspace(SceneWorkspace* workspace) { m_workspace = workspace; }
    // The scene being edited: the one whose view was focused last.
    SceneDocument* GetActiveDocument();

    // How far the mouse moved since the last frame, in pixels, also while it is captured.
    Vector2 GetMouseDelta() const { return m_mouseDelta; }
    void SetMouseDelta(Vector2 delta) { m_mouseDelta = delta; }

private:
    Window& m_window;
    GraphicsDevice* m_device;
    EditorSettings& m_settings;
    std::unique_ptr<EditorProject> m_project;
    SceneWorkspace* m_workspace = nullptr;
    Vector2 m_mouseDelta;
};

} // namespace Spot::Editor
