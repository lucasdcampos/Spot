#pragma once

#include "imgui/imgui_layer.h"
#include "panels/panel.h"

#include <spot.h>

#include <imgui.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace Spot::Editor {

struct EditorOptions {
    // Where the editor keeps its settings and log; the user's data directory when empty.
    std::filesystem::path settingsDirectory;
};

class EditorApplication final : public Application {
public:
    explicit EditorApplication(EditorOptions options = {});
    ~EditorApplication() override;

protected:
    void OnStart() override;
    void OnEvent(const Event& event) override;
    void OnRender(RenderContext& context) override;
    void OnStop() override;
    bool OnCloseRequested() override;

private:
    void DrawUi();
    void DrawMainMenu();
    void DrawToolbar();
    void DrawStatusBar();
    void DrawDockSpace();
    void DrawSceneArea();
    void DrawAboutWindow();

    EditorOptions m_options;
    std::unique_ptr<ImGuiLayer> m_imgui;
    std::vector<std::unique_ptr<Panel>> m_panels;
    ImGuiID m_dockSpace = 0;
    bool m_layoutPending = false;
    bool m_showAbout = false;
    bool m_showImGuiDemo = false;
    bool m_showImGuiMetrics = false;
};

} // namespace Spot::Editor
