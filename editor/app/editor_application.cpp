#include "app/editor_application.h"

#include "app/editor_layout.h"
#include "core/editor_log.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "panels/asset_browser_panel.h"
#include "panels/console_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"

#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Spot::Editor {

namespace {

constexpr const char* SceneAreaId = "Scene";
// Frames per second while the editor is in the background, so it stays live without using the GPU fully.
constexpr int BackgroundFrameRate = 20;

std::filesystem::path ResolveSettingsDirectory(const EditorOptions& options) {
    return options.settingsDirectory.empty() ? GetUserDataDirectory("Spot", "Editor") : options.settingsDirectory;
}

ApplicationConfig MakeConfig(const EditorOptions& options) {
    const std::filesystem::path settings = ResolveSettingsDirectory(options);
    return {
        .window = {.title = "Spot Editor", .width = 1600, .height = 900, .maximized = true},
        // ImGui blends colors as they are displayed; scenes are drawn into their own linear targets.
        .graphics = {.linearWindow = false},
        .logFile = settings.empty() ? std::filesystem::path() : settings / "editor.log",
    };
}

std::string GetShortcutName(ImGuiKeyChord chord) {
    return ImGui::GetKeyChordName(chord);
}

bool MenuItem(const char* label, ImGuiKeyChord shortcut = 0, bool enabled = true) {
    const std::string name = shortcut ? GetShortcutName(shortcut) : std::string();
    return ImGui::MenuItem(label, shortcut ? name.c_str() : nullptr, false, enabled);
}

bool ToolbarButton(const char* icon, const char* tooltip, bool selected = false) {
    const float size = ImGui::GetFrameHeight();
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(EditorColors::Accent.x, EditorColors::Accent.y, EditorColors::Accent.z, 0.35f));
    }
    const bool pressed = ImGui::Button(icon, {size * 1.2f, size});
    if (selected) {
        ImGui::PopStyleColor();
    }
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

void ToolbarSeparator() {
    ImGui::SameLine();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine({position.x, position.y + height * 0.2f},
                                        {position.x, position.y + height * 0.8f},
                                        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::Dummy({1.0f, height});
    ImGui::SameLine();
}

} // namespace

EditorApplication::EditorApplication(EditorOptions options)
    : Application(MakeConfig(options)), m_options(std::move(options)), m_consoleLog(std::make_shared<ConsoleLog>()) {
    m_options.settingsDirectory = ResolveSettingsDirectory(m_options);
    // Added before Run() so the console also shows how the devices started.
    Log::AddSink(m_consoleLog);
}

EditorApplication::~EditorApplication() {
    Log::RemoveSink(m_consoleLog);
}

void EditorApplication::OnStart() {
    m_imgui = std::make_unique<ImGuiLayer>(GetWindow(), GetGraphicsDevice());
    if (!m_options.settingsDirectory.empty()) {
        m_imgui->SetSettingsFile(m_options.settingsDirectory / "layout.ini");
    }

    m_panels.push_back(std::make_unique<HierarchyPanel>());
    m_panels.push_back(std::make_unique<InspectorPanel>());
    m_panels.push_back(std::make_unique<AssetBrowserPanel>());
    m_panels.push_back(std::make_unique<ConsolePanel>(m_consoleLog));
    GetEditorLogger().Info("Spot Editor {} started", GetVersionString());
}

void EditorApplication::OnEvent(const Event& event) {
    m_imgui->ProcessEvent(event);
    if (const auto* focus = event.GetIf<WindowFocusEvent>()) {
        SetMaxFrameRate(focus->focused ? 0 : BackgroundFrameRate);
    }
}

void EditorApplication::OnRender(RenderContext& context) {
    m_imgui->BeginFrame(Time::GetDeltaTime());
    DrawUi();
    m_imgui->EndFrame();

    auto pass = context.BeginRenderPass({.clearColor = Color{0.078f, 0.082f, 0.090f, 1.0f}});
    m_imgui->Render(pass);
}

void EditorApplication::OnStop() {
    m_panels.clear();
    m_imgui.reset();
}

bool EditorApplication::OnCloseRequested() {
    return true;
}

void EditorApplication::DrawUi() {
    DrawMainMenu();
    DrawToolbar();
    DrawStatusBar();
    DrawDockSpace();
    DrawSceneArea();
    for (const auto& panel : m_panels) {
        panel->Draw();
    }
    DrawAboutWindow();
    if (m_showImGuiDemo) {
        ImGui::ShowDemoWindow(&m_showImGuiDemo);
    }
    if (m_showImGuiMetrics) {
        ImGui::ShowMetricsWindow(&m_showImGuiMetrics);
    }
}

void EditorApplication::DrawMainMenu() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        MenuItem(ICON_FA_FILE_CIRCLE_PLUS "  New Project...", 0, false);
        MenuItem(ICON_FA_FOLDER_OPEN "  Open Project...", 0, false);
        if (ImGui::BeginMenu(ICON_FA_CLOCK_ROTATE_LEFT "  Recent Projects", false)) {
            ImGui::EndMenu();
        }
        MenuItem("Close Project", 0, false);
        ImGui::Separator();
        MenuItem(ICON_FA_FILE "  New Scene", ImGuiMod_Ctrl | ImGuiKey_N, false);
        MenuItem(ICON_FA_FILE_IMPORT "  Open Scene...", ImGuiMod_Ctrl | ImGuiKey_O, false);
        MenuItem(ICON_FA_FLOPPY_DISK "  Save Scene", ImGuiMod_Ctrl | ImGuiKey_S, false);
        MenuItem("Save Scene As...", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, false);
        MenuItem("Save All", ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S, false);
        ImGui::Separator();
        MenuItem(ICON_FA_GEAR "  Project Settings...", 0, false);
        ImGui::Separator();
        if (MenuItem("Exit", ImGuiMod_Alt | ImGuiKey_F4) && OnCloseRequested()) {
            Quit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        MenuItem(ICON_FA_ROTATE_LEFT "  Undo", ImGuiMod_Ctrl | ImGuiKey_Z, false);
        MenuItem(ICON_FA_ROTATE_RIGHT "  Redo", ImGuiMod_Ctrl | ImGuiKey_Y, false);
        ImGui::Separator();
        MenuItem(ICON_FA_SCISSORS "  Cut", ImGuiMod_Ctrl | ImGuiKey_X, false);
        MenuItem(ICON_FA_COPY "  Copy", ImGuiMod_Ctrl | ImGuiKey_C, false);
        MenuItem(ICON_FA_PASTE "  Paste", ImGuiMod_Ctrl | ImGuiKey_V, false);
        MenuItem(ICON_FA_CLONE "  Duplicate", ImGuiMod_Ctrl | ImGuiKey_D, false);
        MenuItem(ICON_FA_TRASH_CAN "  Delete", ImGuiKey_Delete, false);
        MenuItem(ICON_FA_PEN "  Rename", ImGuiKey_F2, false);
        ImGui::Separator();
        MenuItem("Select All", ImGuiMod_Ctrl | ImGuiKey_A, false);
        ImGui::Separator();
        MenuItem(ICON_FA_SLIDERS "  Preferences...", 0, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Entity", false)) {
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        for (const auto& panel : m_panels) {
            bool open = panel->IsOpen();
            if (ImGui::MenuItem(panel->GetTitle().c_str(), nullptr, &open)) {
                panel->SetOpen(open);
                if (open) {
                    ImGui::SetWindowFocus(panel->GetWindowName().c_str());
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_TABLE_COLUMNS "  Reset Layout")) {
            for (const auto& panel : m_panels) {
                panel->SetOpen(true);
            }
            m_layoutPending = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO "  About Spot Editor")) {
            m_showAbout = true;
        }
        if (ImGui::BeginMenu("Developer")) {
            ImGui::MenuItem("Dear ImGui Demo", nullptr, &m_showImGuiDemo);
            ImGui::MenuItem("Dear ImGui Metrics", nullptr, &m_showImGuiMetrics);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void EditorApplication::DrawToolbar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, style.Colors[ImGuiCol_MenuBarBg]);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.0f * ImGui::GetStyle().FontScaleDpi, style.ItemSpacing.y});
    if (ImGui::BeginViewportSideBar("##Toolbar", viewport, ImGuiDir_Up, height, flags)) {
        // Tools and play controls arrive with the scene view; they show where they will be meanwhile.
        ImGui::BeginDisabled();
        ToolbarButton(ICON_FA_HAND, "Pan (Q)");
        ImGui::SameLine();
        ToolbarButton(ICON_FA_UP_DOWN_LEFT_RIGHT, "Move (W)", true);
        ImGui::SameLine();
        ToolbarButton(ICON_FA_ROTATE, "Rotate (E)");
        ImGui::SameLine();
        ToolbarButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Scale (R)");
        ToolbarSeparator();
        ToolbarButton(ICON_FA_GLOBE, "World space");
        ImGui::SameLine();
        ToolbarButton(ICON_FA_MAGNET, "Snapping");

        const float playWidth = ImGui::GetFrameHeight() * 1.2f * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
        ImGui::SameLine((ImGui::GetWindowWidth() - playWidth) * 0.5f);
        ToolbarButton(ICON_FA_PLAY, "Play");
        ImGui::SameLine();
        ToolbarButton(ICON_FA_PAUSE, "Pause");
        ImGui::SameLine();
        ToolbarButton(ICON_FA_FORWARD_STEP, "Step");
        ImGui::EndDisabled();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void EditorApplication::DrawStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags)) {
        if (ImGui::BeginMenuBar()) {
            DrawLatestMessage();
            const std::string stats =
                std::format("{:.0f} FPS  |  {}", ImGui::GetIO().Framerate, GetGraphicsDevice().GetBackendName());
            const float width = ImGui::CalcTextSize(stats.c_str()).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x * 2.0f);
            ImGui::TextColored(EditorColors::TextMuted, "%s", stats.c_str());
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApplication::DrawLatestMessage() {
    const std::optional<ConsoleEntry> latest = m_consoleLog->GetLatest();
    if (!latest || latest->level < LogLevel::Info) {
        ImGui::TextColored(EditorColors::TextMuted, "Ready");
        return;
    }
    const ImVec4 color = latest->level == LogLevel::Error     ? EditorColors::Error
                         : latest->level == LogLevel::Warning ? EditorColors::Warning
                                                              : EditorColors::TextMuted;
    const char* icon = latest->level == LogLevel::Error     ? ICON_FA_CIRCLE_XMARK
                       : latest->level == LogLevel::Warning ? ICON_FA_TRIANGLE_EXCLAMATION
                                                            : ICON_FA_CIRCLE_INFO;
    const std::string_view message = std::string_view(latest->message).substr(0, latest->message.find('\n'));
    const std::string text = std::format("{}  {}", icon, message);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    // Clicking the message opens the console, where the whole of it is.
    if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_None, {ImGui::GetWindowWidth() * 0.6f, 0.0f})) {
        for (const auto& panel : m_panels) {
            if (panel->GetId() == "Console") {
                panel->SetOpen(true);
                ImGui::SetWindowFocus(panel->GetWindowName().c_str());
            }
        }
    }
    ImGui::PopStyleColor();
}

void EditorApplication::DrawDockSpace() {
    m_dockSpace = ImGui::GetID("EditorDockSpace");
    // A layout missing from the saved settings, such as on the first run, starts as the default one.
    if (!ImGui::DockBuilderGetNode(m_dockSpace)) {
        m_layoutPending = true;
    }
    if (m_layoutPending) {
        m_layoutPending = false;
        LayoutWindows windows;
        windows.scenes.emplace_back(SceneAreaId);
        for (const auto& panel : m_panels) {
            if (panel->GetId() == "Hierarchy") {
                windows.hierarchy = panel->GetId();
            } else if (panel->GetId() == "Inspector") {
                windows.inspector = panel->GetId();
            } else if (panel->GetId() == "Assets") {
                windows.assets = panel->GetId();
            } else if (panel->GetId() == "Console") {
                windows.console = panel->GetId();
            }
        }
        BuildDefaultLayout(m_dockSpace, windows);
    }
    ImGui::DockSpaceOverViewport(m_dockSpace, ImGui::GetMainViewport());
}

void EditorApplication::DrawSceneArea() {
    if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(m_dockSpace)) {
        ImGui::SetNextWindowDockID(central->ID, ImGuiCond_FirstUseEver);
    }
    const std::string name = std::string(ICON_FA_CUBE "  Scene###") + SceneAreaId;
    if (ImGui::Begin(name.c_str(), nullptr, ImGuiWindowFlags_NoCollapse)) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const char* hint = "No scene is open";
        const char* detail = "Open a project, then create or open a scene.";
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max((available.y - lineHeight * 3.0f) * 0.5f, 0.0f));
        ImGui::PushFont(GetBoldFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(hint).x) * 0.5f);
        ImGui::TextUnformatted(hint);
        ImGui::PopFont();
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(detail).x) * 0.5f);
        ImGui::TextColored(EditorColors::TextMuted, "%s", detail);
    }
    ImGui::End();
}

void EditorApplication::DrawAboutWindow() {
    if (m_showAbout) {
        ImGui::OpenPopup("About Spot Editor");
        m_showAbout = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal("About Spot Editor", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    ImGui::PushFont(GetBoldFont(), ImGui::GetStyle().FontSizeBase * 1.6f);
    ImGui::TextUnformatted("Spot Editor");
    ImGui::PopFont();
    ImGui::TextColored(EditorColors::TextMuted, "Engine %s  |  Dear ImGui %s  |  %s", GetVersionString().data(),
                       ImGui::GetVersion(), GetGraphicsDevice().GetBackendName().data());
    ImGui::Separator();
    ImGui::TextUnformatted("Built with Dear ImGui and ImGuizmo (MIT), the Inter font and Font Awesome Free\n"
                           "(SIL OFL 1.1; icons CC BY 4.0).");
    ImGui::Spacing();
    const float buttonWidth = ImGui::GetFontSize() * 6.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("Close", {buttonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace Spot::Editor
