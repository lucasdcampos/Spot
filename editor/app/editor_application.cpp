#include "app/editor_application.h"

#include "app/editor_layout.h"
#include "core/editor_log.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "panels/asset_browser_panel.h"
#include "panels/console_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/project_settings_panel.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Spot::Editor {

namespace {

constexpr const char* SceneAreaId = "Scene";
constexpr const char* ErrorPopupId = "##EditorError";
// Frames per second while the editor is in the background, so it stays live without using the GPU fully.
constexpr int BackgroundFrameRate = 20;
const std::array ProjectFilters{FileFilter{"Spot projects", "spot"}};

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

// A project's folder stands for its project file, so a folder can be opened as easily as the file.
std::filesystem::path FindProjectFile(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        return path;
    }
    for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        if (entry.path().extension() == Project::FileExtension) {
            return entry.path();
        }
    }
    return path;
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
    : Application(MakeConfig(options)), m_options(std::move(options)), m_consoleLog(std::make_shared<ConsoleLog>()),
      m_self(std::make_shared<EditorApplication*>(this)) {
    m_options.settingsDirectory = ResolveSettingsDirectory(m_options);
    m_settings = EditorSettings(m_options.settingsDirectory.empty() ? std::filesystem::path()
                                                                    : m_options.settingsDirectory / "settings.json");
    // Added before Run() so the console also shows how the devices started.
    Log::AddSink(m_consoleLog);
}

EditorApplication::~EditorApplication() {
    *m_self = nullptr;
    Log::RemoveSink(m_consoleLog);
}

void EditorApplication::RequestOpenProject(std::filesystem::path file) {
    m_projectToOpen = std::move(file);
}

void EditorApplication::RequestCloseProject() {
    m_closeProjectRequested = true;
}

void EditorApplication::OnStart() {
    m_settings.Load();
    m_imgui = std::make_unique<ImGuiLayer>(GetWindow(), GetGraphicsDevice());
    if (!m_options.settingsDirectory.empty()) {
        m_imgui->SetSettingsFile(m_options.settingsDirectory / "layout.ini");
    }

    m_context = std::make_unique<EditorContext>(GetWindow(), &GetGraphicsDevice(), m_settings);
    m_newProjectDialog = std::make_unique<NewProjectDialog>(m_settings, GetWindow());
    m_hub = std::make_unique<ProjectHub>(
        m_settings, ProjectHubActions{
                        .openProject = [this](const std::filesystem::path& file) { RequestOpenProject(file); },
                        .browseForProject = [this] { BrowseForProject(); },
                        .newProject = [this] { m_newProjectDialog->Open(); },
                    });

    m_panels.push_back(std::make_unique<HierarchyPanel>());
    m_panels.push_back(std::make_unique<InspectorPanel>());
    m_panels.push_back(std::make_unique<AssetBrowserPanel>(*m_context));
    m_panels.push_back(std::make_unique<ConsolePanel>(m_consoleLog));
    m_panels.push_back(std::make_unique<ProjectSettingsPanel>(*m_context));
    GetEditorLogger().Info("Spot Editor {} started", GetVersionString());

    if (!m_options.project.empty()) {
        RequestOpenProject(m_options.project);
    }
}

void EditorApplication::OnEvent(const Event& event) {
    m_imgui->ProcessEvent(event);
    if (const auto* focus = event.GetIf<WindowFocusEvent>()) {
        SetMaxFrameRate(focus->focused ? 0 : BackgroundFrameRate);
    } else if (const auto* drop = event.GetIf<FileDropEvent>()) {
        if (FindProjectFile(drop->path).extension() == Project::FileExtension) {
            RequestOpenProject(drop->path);
        }
    }
}

void EditorApplication::OnUpdate(float) {
    if (m_closeProjectRequested) {
        m_closeProjectRequested = false;
        CloseProject();
    }
    if (m_projectToOpen) {
        const std::filesystem::path file = std::move(*m_projectToOpen);
        m_projectToOpen.reset();
        OpenProject(file);
    }
}

void EditorApplication::OnRender(RenderContext& context) {
    m_imgui->BeginFrame(Time::GetDeltaTime());
    DrawUi();
    m_imgui->EndFrame();

    {
        auto pass = context.BeginRenderPass({.clearColor = Color{0.078f, 0.082f, 0.090f, 1.0f}});
        m_imgui->Render(pass);
    }
    ++m_frame;
    if (!m_options.screenshot.empty()) {
        UpdateScreenshot(context);
    }
}

void EditorApplication::UpdateScreenshot(RenderContext& context) {
    GraphicsDevice& device = GetGraphicsDevice();
    if (m_screenshotTarget) {
        // The frame that drew it has been submitted since, so it can be read back now.
        std::vector<std::uint8_t> pixels = device.DownloadTexture(*m_screenshotTarget);
        if (m_screenshotTarget->GetFormat() == TextureFormat::Bgra8 ||
            m_screenshotTarget->GetFormat() == TextureFormat::Bgra8Srgb) {
            for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
                std::swap(pixels[i], pixels[i + 2]);
            }
        }
        const Image image(m_screenshotTarget->GetWidth(), m_screenshotTarget->GetHeight(), std::move(pixels));
        if (image.SavePng(m_options.screenshot)) {
            GetEditorLogger().Info("Saved a screenshot to '{}'", PathToUtf8(m_options.screenshot));
        }
        Quit();
        return;
    }
    if (m_frame >= m_options.screenshotFrame) {
        m_screenshotTarget = device.CreateTexture({.width = static_cast<std::uint32_t>(context.GetWidth()),
                                                   .height = static_cast<std::uint32_t>(context.GetHeight()),
                                                   .format = device.GetWindowFormat(),
                                                   .renderTarget = true});
        auto pass = context.BeginRenderPass(
            {.target = m_screenshotTarget.get(), .clearColor = Color{0.078f, 0.082f, 0.090f, 1.0f}});
        m_imgui->Render(pass);
    }
}

void EditorApplication::OnStop() {
    CloseProject();
    m_settings.Save();
    m_panels.clear();
    m_hub.reset();
    m_newProjectDialog.reset();
    m_context.reset();
    m_screenshotTarget.reset();
    m_imgui.reset();
}

bool EditorApplication::OnCloseRequested() {
    return true;
}

void EditorApplication::OpenProject(const std::filesystem::path& path) {
    const std::filesystem::path file = FindProjectFile(path);
    auto project = EditorProject::Open(file, &GetGraphicsDevice());
    if (!project) {
        ShowError("Could not open the project",
                  std::format("'{}' is not a Spot project, or it could not be read.", PathToUtf8(file)));
        m_settings.RemoveRecentProject(file);
        m_settings.Save();
        return;
    }

    CloseProject();
    Detail::SetAssetManager(&project->GetAssets());
    m_settings.AddRecentProject(project->GetProject().GetFile(), project->GetProject().GetSettings().name);
    m_settings.Save();
    m_context->SetProject(std::move(project));
    UpdateTitle();
}

void EditorApplication::CloseProject() {
    if (!m_context || !m_context->GetProject()) {
        return;
    }
    GetEditorLogger().Info("Closed project '{}'", m_context->GetProject()->GetProject().GetSettings().name);
    // Assets loaded from now on come from the application again, until another project opens.
    Detail::SetAssetManager(&GetAssets());
    m_context->SetProject(nullptr);
    UpdateTitle();
}

void EditorApplication::BrowseForProject() {
    const std::filesystem::path location =
        m_settings.GetRecentProjects().empty()
            ? m_settings.GetProjectsLocation()
            : m_settings.GetRecentProjects().front().file.parent_path().parent_path();
    FileDialog::OpenFile(
        &GetWindow(), ProjectFilters,
        [self = std::weak_ptr(m_self)](std::vector<std::filesystem::path> paths) {
            const auto editor = self.lock();
            if (editor && *editor && !paths.empty()) {
                (*editor)->RequestOpenProject(paths.front());
            }
        },
        false, location);
}

void EditorApplication::UpdateTitle() {
    const EditorProject* project = m_context ? m_context->GetProject() : nullptr;
    GetWindow().SetTitle(project ? project->GetProject().GetSettings().name + " - Spot Editor" : "Spot Editor");
}

void EditorApplication::ShowError(std::string title, std::string message) {
    GetEditorLogger().Error("{}: {}", title, message);
    m_errorTitle = std::move(title);
    m_errorMessage = std::move(message);
    m_showError = true;
}

Panel* EditorApplication::FindPanel(std::string_view id) const {
    const auto found = std::ranges::find_if(m_panels, [&](const auto& panel) { return panel->GetId() == id; });
    return found != m_panels.end() ? found->get() : nullptr;
}

void EditorApplication::DrawUi() {
    DrawMainMenu();
    if (m_context->GetProject()) {
        DrawToolbar();
        DrawStatusBar();
        DrawDockSpace();
        DrawSceneArea();
        for (const auto& panel : m_panels) {
            panel->Draw();
        }
    } else {
        m_hub->Draw();
    }

    if (const auto created = m_newProjectDialog->Draw()) {
        RequestOpenProject(*created);
    }
    DrawAboutWindow();
    DrawErrorWindow();
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
    const bool hasProject = m_context->GetProject() != nullptr;
    if (ImGui::BeginMenu("File")) {
        DrawFileMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit", hasProject)) {
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
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Entity", false)) {
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window", hasProject)) {
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
                panel->SetOpen(panel->GetId() != "ProjectSettings");
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

void EditorApplication::DrawFileMenu() {
    const bool hasProject = m_context->GetProject() != nullptr;
    if (MenuItem(ICON_FA_FILE_CIRCLE_PLUS "  New Project...")) {
        m_newProjectDialog->Open();
    }
    if (MenuItem(ICON_FA_FOLDER_OPEN "  Open Project...")) {
        BrowseForProject();
    }
    if (ImGui::BeginMenu(ICON_FA_CLOCK_ROTATE_LEFT "  Recent Projects", !m_settings.GetRecentProjects().empty())) {
        for (const RecentProject& recent : m_settings.GetRecentProjects()) {
            ImGui::PushID(PathToUtf8(recent.file).c_str());
            if (ImGui::MenuItem(recent.name.c_str())) {
                RequestOpenProject(recent.file);
            }
            ImGui::SetItemTooltip("%s", PathToUtf8(recent.file).c_str());
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    if (MenuItem("Close Project", 0, hasProject)) {
        RequestCloseProject();
    }
    ImGui::Separator();
    MenuItem(ICON_FA_FILE "  New Scene", ImGuiMod_Ctrl | ImGuiKey_N, false);
    MenuItem(ICON_FA_FILE_IMPORT "  Open Scene...", ImGuiMod_Ctrl | ImGuiKey_O, false);
    MenuItem(ICON_FA_FLOPPY_DISK "  Save Scene", ImGuiMod_Ctrl | ImGuiKey_S, false);
    MenuItem("Save Scene As...", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, false);
    MenuItem("Save All", ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S, false);
    ImGui::Separator();
    if (MenuItem(ICON_FA_GEAR "  Project Settings...", 0, hasProject)) {
        if (Panel* settings = FindPanel("ProjectSettings")) {
            settings->SetOpen(true);
            ImGui::SetWindowFocus(settings->GetWindowName().c_str());
        }
    }
    ImGui::Separator();
    if (MenuItem("Exit", ImGuiMod_Alt | ImGuiKey_F4) && OnCloseRequested()) {
        Quit();
    }
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
        if (Panel* console = FindPanel("Console")) {
            console->SetOpen(true);
            ImGui::SetWindowFocus(console->GetWindowName().c_str());
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
        BuildDefaultLayout(m_dockSpace, {.scenes = {SceneAreaId},
                                         .hierarchy = "Hierarchy",
                                         .inspector = "Inspector",
                                         .assets = "Assets",
                                         .console = "Console"});
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
        const char* detail = "Create or open a scene to edit it here.";
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

void EditorApplication::DrawErrorWindow() {
    if (m_showError) {
        ImGui::OpenPopup(ErrorPopupId);
        m_showError = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({ImGui::GetFontSize() * 22.0f, 0.0f}, {ImGui::GetFontSize() * 40.0f, FLT_MAX});
    if (!ImGui::BeginPopupModal(ErrorPopupId, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoTitleBar)) {
        return;
    }
    ImGui::TextColored(EditorColors::Error, ICON_FA_CIRCLE_XMARK);
    ImGui::SameLine();
    ImGui::PushFont(GetBoldFont(), 0.0f);
    ImGui::TextUnformatted(m_errorTitle.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38.0f);
    ImGui::TextUnformatted(m_errorMessage.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const float buttonWidth = ImGui::GetFontSize() * 6.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("OK", {buttonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace Spot::Editor
