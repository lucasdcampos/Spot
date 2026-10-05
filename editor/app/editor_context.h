#pragma once

#include "core/editor_settings.h"
#include "project/editor_project.h"

#include <spot.h>

#include <memory>

namespace Spot::Editor {

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
    void SetProject(std::unique_ptr<EditorProject> project) { m_project = std::move(project); }

private:
    Window& m_window;
    GraphicsDevice* m_device;
    EditorSettings& m_settings;
    std::unique_ptr<EditorProject> m_project;
};

} // namespace Spot::Editor
