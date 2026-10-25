#pragma once

#include "app/editor_context.h"
#include "app/preferences_window.h"
#include "app/shortcuts_window.h"
#include "assets/thumbnail_cache.h"
#include "core/console_log.h"
#include "core/editor_settings.h"
#include "imgui/imgui_layer.h"
#include "modules/module_host.h"
#include "panels/asset_browser_panel.h"
#include "panels/code_editor_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/panel.h"
#include "project/new_project_dialog.h"
#include "project/project_hub.h"
#include "scene/scene_workspace.h"

#include <spot.h>
#include <spot_tools.h>

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Spot::Editor {

struct EditorOptions {
    // Where the editor keeps its settings, layout and log; the user's data directory when empty.
    std::filesystem::path settingsDirectory;
    // A project file, or its folder, to open at start instead of showing the hub.
    std::filesystem::path project;
    // When set, the editor saves a PNG of its window here after a number of frames, then quits; for checking the
    // UI without a person at the screen.
    std::filesystem::path screenshot;
    int screenshotFrame = 60;
    // Tells the user when the editor crashed, and where the report is; off for runs without a person at the screen.
    bool showCrashMessage = false;
};

class EditorApplication final : public PlayHost {
public:
    explicit EditorApplication(EditorOptions options = {});
    ~EditorApplication() override;

    // Projects open and close between frames, never while windows still draw the current one.
    void RequestOpenProject(std::filesystem::path file);
    void RequestCloseProject();
    // Games playing in the editor stop when they quit, on the next frame, so the scenes are not destroyed while they
    // run.
    void QuitGame() override { m_stopPlayingRequested = true; }

protected:
    void OnStart() override;
    void OnEvent(const Event& event) override;
    void OnUpdate(float deltaTime) override;
    void OnRender(RenderContext& context) override;
    void OnStop() override;
    bool OnCloseRequested() override;

private:
    void OpenProject(const std::filesystem::path& file);
    void CloseProject();
    void ConfirmUnsavedChanges(std::function<void()> proceed);
    void BrowseForProject();
    void UpdateTitle();
    void ShowError(std::string title, std::string message);
    Panel* FindPanel(std::string_view id) const;
    void UpdateScreenshot(RenderContext& context);

    void DrawUi();
    void DrawMainMenu();
    void DrawFileMenu();
    void DrawToolbar();
    void DrawGizmoTools();
    void DrawPlayControls();
    // The game's code: whether it builds, and a button to build it.
    void DrawBuildControls();
    void OpenConsole();
    // Builds the game's code when it changed, and loads it once built and nothing plays.
    void UpdateModules();
    // Plays the active scene, once the game's code loads when it is still to, or stops playing or waiting to.
    void TogglePlaying();
    void StartPlaying();
    // Tells why the scene does not play: the game's code did not build, or did not load.
    void ShowPlayError(const ModuleHost& modules);
    // Runs the game in its own window, once its scenes are saved and its code is built.
    void RunGame();
    void DrawStatusBar();
    void DrawLatestMessage();
    void DrawDockSpace();
    void HandleShortcuts();
    // What Ctrl+Z undoes: the inspected material's edits while the inspector has focus, or else the active scene's.
    CommandHistory* GetFocusedHistory();
    void Undo();
    void Redo();
    void DrawEntityMenu(SceneDocument& document);
    void DrawAboutWindow();
    void DrawErrorWindow();

    EditorOptions m_options;
    EditorSettings m_settings;
    std::shared_ptr<ConsoleLog> m_consoleLog;
    std::unique_ptr<ImGuiLayer> m_imgui;
    std::unique_ptr<EditorContext> m_context;
    std::unique_ptr<SceneWorkspace> m_workspace;
    std::unique_ptr<ThumbnailCache> m_thumbnails;
    std::unique_ptr<GameRunner> m_game;
    // Set while the game waits for its code to build before it runs.
    bool m_runPending = false;
    bool m_stopPlayingRequested = false;
    // Seconds since the scenes were last backed up.
    float m_sinceBackup = 0.0f;
    std::unique_ptr<ProjectHub> m_hub;
    std::unique_ptr<NewProjectDialog> m_newProjectDialog;
    std::unique_ptr<PreferencesWindow> m_preferences;
    ShortcutsWindow m_shortcuts;
    std::vector<std::unique_ptr<Panel>> m_panels;
    InspectorPanel* m_inspector = nullptr;
    HierarchyPanel* m_hierarchy = nullptr;
    AssetBrowserPanel* m_assetBrowser = nullptr;
    CodeEditorPanel* m_codeEditor = nullptr;
    // OS dialogs answer later; they only reach the editor while this is alive.
    std::shared_ptr<EditorApplication*> m_self;
    int m_frame = 0;
    std::unique_ptr<Texture> m_screenshotTarget;

    std::optional<std::filesystem::path> m_projectToOpen;
    bool m_closeProjectRequested = false;
    // Set once the user agreed to quit, after being asked about unsaved changes.
    bool m_quitConfirmed = false;
    // Summed from mouse events until the next frame.
    Vector2 m_mouseDelta;
    std::string m_errorTitle;
    std::string m_errorMessage;
    bool m_showError = false;

    ImGuiID m_dockSpace = 0;
    bool m_layoutPending = false;
    bool m_showAbout = false;
    bool m_showImGuiDemo = false;
    bool m_showImGuiMetrics = false;
};

} // namespace Spot::Editor
