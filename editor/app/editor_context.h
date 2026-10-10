#pragma once

#include "core/editor_settings.h"
#include "project/editor_project.h"
#include "scene/scene_document.h"

#include <spot.h>

#include <cstdint>
#include <memory>

namespace Spot::Editor {

class SceneWorkspace;
class ThumbnailCache;

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

    // Shows the asset in the inspector, until the active scene's selection changes, as when an asset is clicked in
    // the asset browser; the null id goes back to the selected entities.
    void InspectAsset(AssetId asset);
    // The asset the inspector shows instead of the selected entities; the null id when it shows them.
    AssetId GetInspectedAsset();

    // Pictures of assets; null without a graphics device, as in tests.
    ThumbnailCache* GetThumbnails() { return m_thumbnails; }
    void SetThumbnails(ThumbnailCache* thumbnails) { m_thumbnails = thumbnails; }

    // The editor's own input, also while a game plays in it; null in tests that have none.
    const InputState* GetInput() const { return m_input; }
    void SetInput(const InputState* input) { m_input = input; }

    // How far the mouse moved since the last frame, in pixels, also while it is captured.
    Vector2 GetMouseDelta() const { return m_mouseDelta; }
    void SetMouseDelta(Vector2 delta) { m_mouseDelta = delta; }

private:
    Window& m_window;
    GraphicsDevice* m_device;
    EditorSettings& m_settings;
    std::unique_ptr<EditorProject> m_project;
    SceneWorkspace* m_workspace = nullptr;
    ThumbnailCache* m_thumbnails = nullptr;
    AssetId m_inspectedAsset;
    // The active scene and its selection's version when the asset was inspected.
    std::uint64_t m_inspectedDocument = 0;
    std::uint64_t m_inspectedSelection = 0;
    Vector2 m_mouseDelta;
    const InputState* m_input = nullptr;
};

} // namespace Spot::Editor
