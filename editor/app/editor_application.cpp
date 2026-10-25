
#include <spot/graphics/image.h>
#include <spot/scene/scripting/scripting_module.h>

#include "app/editor_application.h"

#include "app/editor_layout.h"
#include "code/script_editor.h"
#include "core/editor_log.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"
#include "panels/asset_browser_panel.h"
#include "panels/audio_mixer_panel.h"
#include "panels/console_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/profiler_panel.h"
#include "panels/project_settings_panel.h"
#include "project/external_editor.h"
#include "scene/entity_actions.h"

#include <imgui_internal.h>

#include <ImGuizmo.h>

#include "icon.png.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
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
constexpr const char* OpeningPopupId = "##OpeningProject";
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
        .showCrashMessage = options.showCrashMessage,
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
    return Widgets::GetKeyChordLabel(chord);
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
    : PlayHost(MakeConfig(options)), m_options(std::move(options)), m_consoleLog(std::make_shared<ConsoleLog>()),
      m_self(std::make_shared<EditorApplication*>(this)) {
    m_options.settingsDirectory = ResolveSettingsDirectory(m_options);
    m_settings = EditorSettings(m_options.settingsDirectory.empty() ? std::filesystem::path()
                                                                    : m_options.settingsDirectory / "settings.json");
    // Added before Run() so the console also shows how the devices started.
    Log::AddSink(m_consoleLog);
    // The editor has a console of its own.
    AddModule<DevToolsModule>(DevToolsConfig{.consoleOverlay = false});
    AddModule<UiModule>();
    // The scenes show in the editor's views, which draw them themselves.
    AddModule<RenderModule>(RenderModuleConfig{.drawScene = false});
    AddModule<AudioModule>();
    AddModule<PhysicsModule>();
    AddModule<Physics2DModule>();
    AddModule<AnimationModule>();
    AddModule<ParticlesModule>();
    AddModule<ScriptingModule>();
}

EditorApplication::~EditorApplication() {
    *m_self = nullptr;
    Log::RemoveSink(m_consoleLog);
}

void EditorApplication::RequestOpenProject(std::filesystem::path file) {
    // The scenes of the open project are closed first, so unsaved changes are asked about.
    ConfirmUnsavedChanges([this, file = std::move(file)] { m_projectToOpen = file; });
}

void EditorApplication::RequestCloseProject() {
    ConfirmUnsavedChanges([this] { m_closeProjectRequested = true; });
}

void EditorApplication::ConfirmUnsavedChanges(std::function<void()> proceed) {
    m_codeEditor->ConfirmAllUnsavedChanges(
        [this, proceed = std::move(proceed)] { m_workspace->ConfirmAllUnsavedChanges(proceed); });
}

void EditorApplication::OnStart() {
    m_settings.Load();
    if (const auto icon = Image::LoadFromMemory(std::as_bytes(std::span(Resources::IconPng)))) {
        GetWindow().SetIcon(icon->GetPixels(), static_cast<int>(icon->GetWidth()), static_cast<int>(icon->GetHeight()));
    }
    m_imgui = std::make_unique<ImGuiLayer>(GetWindow(), GetGraphicsDevice());
    if (!m_options.settingsDirectory.empty()) {
        m_imgui->SetSettingsFile(m_options.settingsDirectory / "layout.ini");
    }

    m_context = std::make_unique<EditorContext>(GetWindow(), &GetGraphicsDevice(), m_settings);
    m_context->SetInput(&GetInputState());
    m_context->SetAudioDevice(&GetModule<AudioModule>()->GetDevice());
    m_thumbnails = std::make_unique<ThumbnailCache>(GetGraphicsDevice());
    m_game = std::make_unique<GameRunner>();
    m_context->SetThumbnails(m_thumbnails.get());
    m_workspace = std::make_unique<SceneWorkspace>(*m_context);
    m_context->SetWorkspace(m_workspace.get());
    m_newProjectDialog = std::make_unique<NewProjectDialog>(m_settings, GetWindow());
    m_preferences = std::make_unique<PreferencesWindow>(m_settings, GetWindow());
    m_hub = std::make_unique<ProjectHub>(
        m_settings, ProjectHubActions{
                        .openProject = [this](const std::filesystem::path& file) { RequestOpenProject(file); },
                        .browseForProject = [this] { BrowseForProject(); },
                        .newProject = [this] { m_newProjectDialog->Open(); },
                    });

    auto hierarchy = std::make_unique<HierarchyPanel>(*m_context);
    m_hierarchy = hierarchy.get();
    m_panels.push_back(std::move(hierarchy));
    auto inspector = std::make_unique<InspectorPanel>(*m_context);
    m_inspector = inspector.get();
    m_panels.push_back(std::move(inspector));
    auto assetBrowser = std::make_unique<AssetBrowserPanel>(*m_context);
    m_assetBrowser = assetBrowser.get();
    m_panels.push_back(std::move(assetBrowser));
    auto codeEditor = std::make_unique<CodeEditorPanel>(*m_context);
    m_codeEditor = codeEditor.get();
    m_context->SetCodeEditor(m_codeEditor);
    m_panels.push_back(std::move(codeEditor));
    m_panels.push_back(std::make_unique<ConsolePanel>(m_consoleLog));
    m_panels.push_back(std::make_unique<ProfilerPanel>(
        [this] { return m_game ? m_game->GetProfileFrames() : std::vector<const ProfileFrame*>{}; }));
    m_panels.push_back(std::make_unique<ProjectSettingsPanel>(*m_context));
    m_panels.push_back(std::make_unique<AudioMixerPanel>(*m_context));
    GetEditorLogger().Info("Spot Editor {} started", GetVersionString());

    if (!m_options.project.empty()) {
        RequestOpenProject(m_options.project);
    }
}

void EditorApplication::OnEvent(const Event& event) {
    m_imgui->ProcessEvent(event);
    if (PlaySession* play = m_workspace ? m_workspace->GetPlaySession() : nullptr) {
        play->Feed(event);
    }
    if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
        m_mouseDelta += moved->delta;
    } else if (const auto* focus = event.GetIf<WindowFocusEvent>()) {
        SetMaxFrameRate(focus->focused ? 0 : BackgroundFrameRate);
        // Files may have changed in other applications meanwhile.
        if (EditorProject* project = m_context->GetProject(); project && focus->focused && !m_opening) {
            project->Refresh();
        }
    } else if (const auto* drop = event.GetIf<FileDropEvent>()) {
        if (FindProjectFile(drop->path).extension() == Project::FileExtension) {
            RequestOpenProject(drop->path);
        } else if (m_context->GetProject() && m_assetBrowser) {
            m_assetBrowser->DropFile(drop->path, {drop->position.x, drop->position.y});
        }
    }
}

void EditorApplication::OnUpdate(float /*deltaTime*/) {
    if (m_closeProjectRequested) {
        m_closeProjectRequested = false;
        CloseProject();
    }
    if (m_projectToOpen) {
        const std::filesystem::path file = std::move(*m_projectToOpen);
        m_projectToOpen.reset();
        OpenProject(file);
    }
    if (m_opening && m_opening->IsDone()) {
        FinishOpeningProject();
    }
    if (std::exchange(m_stopPlayingRequested, false) && m_workspace->GetPlaySession()) {
        m_workspace->StopPlaying();
    }
    UpdateModules();

    const int backupMinutes = m_settings.GetPreferences().backupMinutes;
    m_sinceBackup += Time::GetUnscaledDeltaTime();
    if (backupMinutes > 0 && m_sinceBackup >= static_cast<float>(backupMinutes) * 60.0f) {
        m_sinceBackup = 0.0f;
        m_workspace->BackUp();
    }
}

void EditorApplication::UpdateModules() {
    m_game->Update();
    EditorProject* project = m_context->GetProject();
    if (!project) {
        return;
    }
    ModuleHost& modules = project->GetModules();
    modules.SetBuildOnChange(m_settings.GetPreferences().buildOnChange);
    modules.Update();
    if (m_runPending && !modules.GetBuild().IsRunning()) {
        m_runPending = false;
        if (modules.GetBuild().GetState() == CodeBuildState::Failed) {
            ShowError("The game did not build", "The game's code has errors, which the console shows. Run the game "
                                                "again once they are fixed.");
        } else if (const auto launch = PrepareGameRun(project->GetProject())) {
            m_game->Start(launch->executable, launch->arguments);
        }
    }
    // A game that stopped leaves its scenes, with components of the old code, until the frame after.
    if (modules.HasNewModule() && !m_workspace->GetPlaySession() && !GetScene()) {
        const auto started = std::chrono::steady_clock::now();
        m_workspace->ReloadScenes([&modules] { modules.SwapModule(); });
        const auto now = std::chrono::steady_clock::now();
        // From the change seen to the scenes running the new code: what game code iteration costs.
        GetEditorLogger().Info("Reloaded the game's code in {:.0f} ms (module {:.0f} ms), {:.1f} s after the change",
                               std::chrono::duration<float, std::milli>(now - started).count(),
                               std::chrono::duration<float, std::milli>(modules.GetLoadDuration()).count(),
                               std::chrono::duration<float>(now - modules.GetChangeTime()).count());
    }
    if (m_workspace->IsPlayPending() && !modules.IsLoading()) {
        m_workspace->SetPlayPending(false);
        if (modules.GetModule()) {
            StartPlaying();
        } else {
            ShowPlayError(modules);
        }
    }
}

void EditorApplication::OnRender(RenderContext& context) {
    m_context->SetMouseDelta(std::exchange(m_mouseDelta, Vector2{}));
    m_imgui->SetUiScale(m_settings.GetPreferences().uiScale);
    m_imgui->BeginFrame(Time::GetUnscaledDeltaTime());
    DrawUi();
    m_imgui->EndFrame();

    // The views' scenes and the assets' pictures are drawn before the window, which shows them.
    m_workspace->Render(context);
    if (EditorProject* project = m_context->GetProject()) {
        m_thumbnails->Render(context, *project);
    }

    {
        auto pass = context.BeginRenderPass({.clearColor = Color{0.078f, 0.082f, 0.090f, 1.0f}});
        m_imgui->Render(pass);
    }
    // Screenshots wait for the project to open, however long its assets take to import.
    if (!m_opening) {
        ++m_frame;
    }
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
    // A project still opening waits for its import to end.
    m_opening.reset();
    CloseProject();
    m_settings.Save();
    m_inspector = nullptr;
    m_assetBrowser = nullptr;
    m_hierarchy = nullptr;
    m_context->SetCodeEditor(nullptr);
    m_codeEditor = nullptr;
    m_panels.clear();
    m_hub.reset();
    m_newProjectDialog.reset();
    m_preferences.reset();
    m_context->SetWorkspace(nullptr);
    m_workspace.reset();
    m_context.reset();
    m_thumbnails.reset();
    m_game.reset();
    m_screenshotTarget.reset();
    m_imgui.reset();
}

bool EditorApplication::OnCloseRequested() {
    if (m_quitConfirmed || (!m_workspace->HasUnsavedChanges() && !m_codeEditor->HasUnsavedChanges())) {
        return true;
    }
    ConfirmUnsavedChanges([this] {
        m_quitConfirmed = true;
        Quit();
    });
    return false;
}

void EditorApplication::OpenProject(const std::filesystem::path& path) {
    if (m_opening) {
        return;
    }
    const std::filesystem::path file = FindProjectFile(path);
    m_opening = EditorProject::BeginOpen(file);
    if (!m_opening) {
        ShowError("Could not open the project",
                  std::format("'{}' is not a Spot project, or it could not be read.", PathToUtf8(file)));
        m_settings.RemoveRecentProject(file);
        m_settings.Save();
        return;
    }
    // Its changes were asked about before, and nothing may use the import folder while the new project imports,
    // which it shares when the same project opens again.
    CloseProject();
}

void EditorApplication::FinishOpeningProject() {
    auto project = std::exchange(m_opening, nullptr)->Finish(&GetGraphicsDevice());
    Detail::SetAssetManager(&project->GetAssets());
    m_settings.AddRecentProject(project->GetProject().GetFile(), project->GetProject().GetSettings().name);
    m_settings.Save();
    m_context->SetProject(std::move(project));
    // Before the scenes open, so they find the game's components.
    m_context->GetProject()->GetModules().LoadBuilt();
    m_workspace->RestoreState();
    m_workspace->OfferRecovery();
    m_sinceBackup = 0.0f;
    UpdateTitle();
}

void EditorApplication::CloseProject() {
    if (!m_context || !m_context->GetProject()) {
        return;
    }
    GetEditorLogger().Info("Closed project '{}'", m_context->GetProject()->GetProject().GetSettings().name);
    m_game->Stop();
    m_runPending = false;
    // Changes were asked about before, so the scenes close as they are.
    m_workspace->SaveState();
    m_workspace->CloseAll();
    m_codeEditor->CloseAll();
    // Assets loaded from now on come from the application again, until another project opens.
    Detail::SetAssetManager(&GetAssets());
    m_thumbnails->Clear();
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
    ImGuizmo::BeginFrame();
    // The editor is tinted while a game plays, so changes that will be undone when it stops are not mistaken for
    // lasting ones.
    const bool playing = m_workspace && m_workspace->GetPlaySession();
    if (playing) {
        const ImGuiStyle& style = ImGui::GetStyle();
        constexpr std::array Tinted{ImGuiCol_MenuBarBg, ImGuiCol_TitleBg,       ImGuiCol_TitleBgActive,
                                    ImGuiCol_Tab,       ImGuiCol_TabDimmed,     ImGuiCol_WindowBg,
                                    ImGuiCol_ChildBg,   ImGuiCol_DockingEmptyBg};
        for (const ImGuiCol color : Tinted) {
            const ImVec4 base = style.Colors[color];
            const ImVec4 tint = EditorColors::Accent;
            // No tint for now; raise it to bring the tint back.
            constexpr float Amount = 0.0f;
            ImGui::PushStyleColor(color,
                                  ImVec4(base.x + (tint.x - base.x) * Amount, base.y + (tint.y - base.y) * Amount,
                                         base.z + (tint.z - base.z) * Amount, base.w));
        }
    }
    // An edit that just ended goes in the history before a menu or a shortcut can undo.
    if (m_inspector) {
        m_inspector->FinishEdit();
    }
    DrawMainMenu();
    if (m_context->GetProject()) {
        DrawToolbar();
        DrawStatusBar();
        DrawDockSpace();
        const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(m_dockSpace);
        m_workspace->Draw(central ? central->ID : m_dockSpace);
        for (const auto& panel : m_panels) {
            if (panel.get() == m_codeEditor && panel->IsOpen()) {
                ImGui::SetNextWindowDockID(central ? central->ID : m_dockSpace, ImGuiCond_FirstUseEver);
            }
            panel->Draw();
        }
        HandleShortcuts();
    } else {
        m_hub->Draw();
    }

    if (const auto created = m_newProjectDialog->Draw()) {
        RequestOpenProject(*created);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Comma, ImGuiInputFlags_RouteGlobal)) {
        m_preferences->Open();
    }
    if (ImGui::Shortcut(ImGuiKey_F1, ImGuiInputFlags_RouteGlobal)) {
        m_shortcuts.Open();
    }
    m_preferences->Draw();
    m_shortcuts.Draw();
    DrawAboutWindow();
    DrawOpeningWindow();
    DrawErrorWindow();
    if (m_showImGuiDemo) {
        ImGui::ShowDemoWindow(&m_showImGuiDemo);
    }
    if (m_showImGuiMetrics) {
        ImGui::ShowMetricsWindow(&m_showImGuiMetrics);
    }
    if (playing) {
        ImGui::PopStyleColor(8);
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
    if (ImGui::BeginMenu("Edit")) {
        TextEditor* code = m_codeEditor->IsFocused() ? m_codeEditor->GetActiveEditor() : nullptr;
        CommandHistory* history = GetFocusedHistory();
        const std::string undo = history && history->CanUndo() ? ICON_FA_ROTATE_LEFT "  Undo " + history->GetUndoName()
                                                               : std::string(ICON_FA_ROTATE_LEFT "  Undo");
        const std::string redo = history && history->CanRedo() ? ICON_FA_ROTATE_RIGHT "  Redo " + history->GetRedoName()
                                                               : std::string(ICON_FA_ROTATE_RIGHT "  Redo");
        if (MenuItem(undo.c_str(), ImGuiMod_Ctrl | ImGuiKey_Z,
                     code ? code->CanUndo() : history && history->CanUndo())) {
            Undo();
        }
        if (MenuItem(redo.c_str(), ImGuiMod_Ctrl | ImGuiKey_Y,
                     code ? code->CanRedo() : history && history->CanRedo())) {
            Redo();
        }
        ImGui::Separator();
        SceneDocument* document = m_workspace->GetActiveDocument();
        const bool hasSelection = document && !document->GetSelection().IsEmpty();
        if (MenuItem(ICON_FA_SCISSORS "  Cut", ImGuiMod_Ctrl | ImGuiKey_X,
                     code ? code->AnyCursorHasSelection() : hasSelection)) {
            if (code)
                code->Cut();
            else
                CutSelection(*document);
        }
        if (MenuItem(ICON_FA_COPY "  Copy", ImGuiMod_Ctrl | ImGuiKey_C,
                     code ? code->AnyCursorHasSelection() : hasSelection)) {
            if (code)
                code->Copy();
            else
                CopySelection(*document);
        }
        if (MenuItem(ICON_FA_PASTE "  Paste", ImGuiMod_Ctrl | ImGuiKey_V, code || document)) {
            if (code)
                code->Paste();
            else
                PasteClipboard(*document);
        }
        if (MenuItem(ICON_FA_CLONE "  Duplicate", ImGuiMod_Ctrl | ImGuiKey_D, !code && hasSelection)) {
            DuplicateSelection(*document);
        }
        if (MenuItem(ICON_FA_TRASH_CAN "  Delete", ImGuiKey_Delete, !code && hasSelection)) {
            DeleteSelection(*document);
        }
        if (MenuItem(ICON_FA_PEN "  Rename", ImGuiKey_F2, !code && hasSelection && m_hierarchy)) {
            m_hierarchy->SetOpen(true);
            m_hierarchy->BeginRename(document->GetSelection().GetPrimary());
        }
        ImGui::Separator();
        if (MenuItem("Select All", ImGuiMod_Ctrl | ImGuiKey_A, code || document)) {
            if (code)
                code->SelectAll();
            else
                SelectAll(*document);
        }
        ImGui::Separator();
        if (MenuItem(ICON_FA_SLIDERS "  Preferences...", ImGuiMod_Ctrl | ImGuiKey_Comma)) {
            m_preferences->Open();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Entity", m_workspace->GetActiveDocument() != nullptr)) {
        DrawEntityMenu(*m_workspace->GetActiveDocument());
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
                // Windows opened when needed start closed.
                panel->SetOpen(panel->GetId() != "ProjectSettings" && panel->GetId() != "AudioMixer" &&
                               panel->GetId() != "Code");
            }
            m_layoutPending = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (MenuItem(ICON_FA_KEYBOARD "  Keyboard Shortcuts", ImGuiKey_F1)) {
            m_shortcuts.Open();
        }
        ImGui::Separator();
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
    SceneDocument* document = m_workspace->GetActiveDocument();
    const bool editingCode = m_codeEditor->IsFocused() && m_codeEditor->GetActiveEditor();
    if (MenuItem(ICON_FA_FILE "  New Scene", ImGuiMod_Ctrl | ImGuiKey_N, hasProject)) {
        m_workspace->NewScene();
    }
    if (MenuItem(ICON_FA_FILE_IMPORT "  Open Scene...", ImGuiMod_Ctrl | ImGuiKey_O, hasProject)) {
        m_workspace->BrowseForScene();
    }
    if (MenuItem(editingCode ? ICON_FA_FLOPPY_DISK "  Save Script" : ICON_FA_FLOPPY_DISK "  Save Scene",
                 ImGuiMod_Ctrl | ImGuiKey_S, editingCode || document)) {
        if (editingCode)
            m_codeEditor->SaveActive();
        else
            m_workspace->Save(*document);
    }
    if (MenuItem("Save Scene As...", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, !editingCode && document)) {
        m_workspace->SaveAs(*document);
    }
    if (MenuItem("Save All", ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S,
                 m_workspace->GetDocumentCount() > 0 || m_codeEditor->GetDocumentCount() > 0)) {
        m_codeEditor->SaveAll();
        m_workspace->SaveAll();
    }
    if (MenuItem(editingCode ? "Close Script" : "Close Scene", ImGuiMod_Ctrl | ImGuiKey_W, editingCode || document)) {
        if (editingCode)
            m_codeEditor->CloseActive();
        else
            m_workspace->Close(*document);
    }
    ImGui::Separator();
    const bool hasCode = hasProject && m_context->GetProject()->GetModules().HasCode();
    if (MenuItem(ICON_FA_HAMMER "  Build Game Code", ImGuiMod_Ctrl | ImGuiKey_B, hasCode)) {
        m_context->GetProject()->GetModules().Build(ModuleHost::BuildScope::Game);
    }
    if (MenuItem(ICON_FA_CODE "  Open in External Editor", 0, hasProject)) {
        OpenInExternalEditor(m_settings.GetPreferences().externalEditor, m_context->GetProject()->GetProject());
    }
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
        DrawGizmoTools();
        DrawPlayControls();
        DrawBuildControls();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void EditorApplication::DrawPlayControls() {
    PlaySession* play = m_workspace->GetPlaySession();
    // Centered on the play, pause and step buttons; running the game in its own window comes after them.
    const float playWidth = ImGui::GetFrameHeight() * 1.2f * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SameLine((ImGui::GetWindowWidth() - playWidth) * 0.5f);
    const bool pending = m_workspace->IsPlayPending();
    ImGui::BeginDisabled(!play && !m_workspace->GetActiveDocument());
    if (ToolbarButton(play      ? ICON_FA_STOP
                      : pending ? ICON_FA_HOURGLASS_HALF
                                : ICON_FA_PLAY,
                      play      ? "Stop (Ctrl+P): what changed while playing is undone"
                      : pending ? "The scene plays as soon as the game's code is built and loaded; click to cancel "
                                  "(Ctrl+P)"
                                : "Play the scene (Ctrl+P)",
                      play != nullptr || pending)) {
        TogglePlaying();
        // Stopping ends the session.
        play = m_workspace->GetPlaySession();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!play);
    const bool paused = play && play->IsPaused();
    if (ToolbarButton(ICON_FA_PAUSE, paused ? "Resume (Ctrl+Shift+P)" : "Pause (Ctrl+Shift+P)", paused)) {
        play->SetPaused(!paused);
    }
    ImGui::SameLine();
    if (ToolbarButton(ICON_FA_FORWARD_STEP, "Run one frame, paused (Ctrl+Alt+P)")) {
        play->Step();
    }
    ImGui::EndDisabled();

    ToolbarSeparator();
    const bool running = m_game->IsRunning() || m_runPending;
    if (ToolbarButton(ICON_FA_UP_RIGHT_FROM_SQUARE,
                      running ? "Stop the game running in its own window (Shift+F5)"
                              : "Run the game in its own window, from its startup scene, as it will ship (F5); its "
                                "scenes are saved first",
                      running)) {
        if (running) {
            m_runPending = false;
            m_game->Stop();
        } else {
            RunGame();
        }
    }
}

void EditorApplication::RunGame() {
    const EditorProject* project = m_context->GetProject();
    if (!project->GetProject().GetSettings().startupScene.IsValid()) {
        ShowError("Nothing to run", "The project has no startup scene. Choose the scene its game starts with in the "
                                    "project settings.");
        return;
    }
    // The game runs what is saved.
    for (std::size_t i = 0; i < m_workspace->GetDocumentCount(); ++i) {
        SceneDocument& document = m_workspace->GetDocument(i);
        if (!document.GetFile().empty() && document.IsDirty()) {
            m_workspace->Save(document);
        }
    }
    // The game runs the code as it is now, which its executable may not have yet: builds on change only make the
    // module. Built already, it takes a moment to find nothing to do.
    m_context->GetProject()->GetModules().Build(ModuleHost::BuildScope::Game);
    m_runPending = true;
}

void EditorApplication::DrawBuildControls() {
    ModuleHost& modules = m_context->GetProject()->GetModules();
    if (!modules.HasCode()) {
        return;
    }
    const CodeBuild& build = modules.GetBuild();
    std::string status;
    // As wide as the status gets, so it does not move as it changes.
    std::string widest;
    ImVec4 color = EditorColors::TextMuted;
    const char* tooltip = nullptr;
    if (build.IsRunning()) {
        // The dots move so it shows that something happens.
        const auto dots = static_cast<std::size_t>(ImGui::GetTime() * 3.0) % 4;
        const char* building =
            m_workspace->IsPlayPending() ? ICON_FA_GEARS "  Building, then playing" : ICON_FA_GEARS "  Building";
        status = building + std::string(dots, '.');
        widest = building + std::string("...");
        tooltip = m_workspace->IsPlayPending()
                      ? "The game's code is building; the scene plays as soon as it is built and loaded."
                      : "The game's code is building; the editor keeps the code it has until it is done.";
    } else if (modules.HasNewModule()) {
        status = ICON_FA_CLOCK "  New code loads when the game stops";
    } else if (modules.HasUnbuiltChanges()) {
        status = ICON_FA_CODE "  Code changed";
        tooltip = "The game's code changed since it was built; Ctrl+B builds it. Building as it changes can be "
                  "turned on in Preferences.";
    } else if (build.GetState() == CodeBuildState::Failed) {
        status = ICON_FA_CIRCLE_XMARK "  Build failed";
        color = EditorColors::Error;
        tooltip = "The game's code did not build; click to see why in the console. The editor keeps the code it had.";
    }
    if (widest.empty()) {
        widest = status;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonWidth = ImGui::GetFrameHeight() * 1.2f;
    const float statusWidth = status.empty() ? 0.0f : ImGui::CalcTextSize(widest.c_str()).x + style.ItemSpacing.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - style.WindowPadding.x - buttonWidth - statusWidth);
    if (!status.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        if (ImGui::Selectable(status.c_str(), false, ImGuiSelectableFlags_None,
                              {statusWidth - style.ItemSpacing.x, ImGui::GetFrameHeight()})) {
            OpenConsole();
        }
        ImGui::PopStyleColor();
        if (tooltip) {
            ImGui::SetItemTooltip("%s", tooltip);
        }
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(build.IsRunning());
    if (ToolbarButton(ICON_FA_HAMMER, "Build the game's code (Ctrl+B); it also builds by itself when it changes")) {
        modules.Build(ModuleHost::BuildScope::Game);
    }
    ImGui::EndDisabled();
}

void EditorApplication::OpenConsole() {
    if (Panel* console = FindPanel("Console")) {
        console->SetOpen(true);
        ImGui::SetWindowFocus(console->GetWindowName().c_str());
    }
}

void EditorApplication::TogglePlaying() {
    if (m_workspace->GetPlaySession()) {
        m_workspace->StopPlaying();
        return;
    }
    if (m_workspace->IsPlayPending()) {
        m_workspace->SetPlayPending(false);
        return;
    }
    // Played before the game's code loads, as while it builds after the engine changed, the scene would have none of
    // its components: it plays once the code loads instead.
    EditorProject* project = m_context->GetProject();
    if (!project) {
        return;
    }
    ModuleHost& modules = project->GetModules();
    if (modules.HasCode() && !modules.GetModule()) {
        if (!modules.IsLoading()) {
            if (modules.GetBuild().GetState() == CodeBuildState::Failed && !modules.HasUnbuiltChanges()) {
                ShowPlayError(modules);
                return;
            }
            modules.Build();
        }
        m_workspace->SetPlayPending(true);
        return;
    }
    StartPlaying();
}

void EditorApplication::StartPlaying() {
    if (m_settings.GetPreferences().clearConsoleOnPlay) {
        m_consoleLog->Clear();
    }
    m_workspace->Play(*this);
}

void EditorApplication::ShowPlayError(const ModuleHost& modules) {
    if (modules.GetBuild().GetState() == CodeBuildState::Failed) {
        ShowError("The game did not build", "The game's code has errors, which the console shows. Play the scene "
                                            "again once they are fixed.");
    } else {
        ShowError("The game's code did not load", "The game's code built but did not load; the console shows why.");
    }
}

void EditorApplication::DrawGizmoTools() {
    GizmoSettings& gizmo = m_settings.GetGizmo();
    const auto tool = [&](const char* icon, const char* tooltip, GizmoTool value) {
        if (ToolbarButton(icon, tooltip, gizmo.tool == value)) {
            gizmo.tool = value;
        }
        ImGui::SameLine();
    };
    tool(ICON_FA_ARROW_POINTER, "Select (Q)", GizmoTool::Select);
    tool(ICON_FA_UP_DOWN_LEFT_RIGHT, "Move (W)", GizmoTool::Move);
    tool(ICON_FA_ROTATE, "Rotate (E)", GizmoTool::Rotate);
    tool(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Scale (R)", GizmoTool::Scale);
    ToolbarSeparator();

    if (ToolbarButton(gizmo.local ? ICON_FA_CUBE : ICON_FA_GLOBE,
                      gizmo.local ? "Local: the handles follow the entity's axes (X)"
                                  : "Global: the handles follow the world's axes (X)")) {
        gizmo.local = !gizmo.local;
    }
    ImGui::SameLine();
    if (ToolbarButton(ICON_FA_MAGNET,
                      "Snapping: move, rotate and scale in steps; Ctrl while dragging does the opposite", gizmo.snap)) {
        gizmo.snap = !gizmo.snap;
    }
    // The steps, in a menu beside the button.
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {2.0f * ImGui::GetStyle().FontScaleDpi, ImGui::GetStyle().FramePadding.y});
    if (ImGui::Button(ICON_FA_CARET_DOWN "##SnapSteps", {0.0f, ImGui::GetFrameHeight()})) {
        ImGui::OpenPopup("##SnapSteps");
    }
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip("Snapping steps");
    if (ImGui::BeginPopup("##SnapSteps")) {
        ImGui::TextDisabled("Snapping steps");
        if (Widgets::BeginProperties("##Steps")) {
            Widgets::PropertyLabel("Move");
            ImGui::DragFloat("##Move", &gizmo.moveStep, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            Widgets::PropertyLabel("Rotate");
            ImGui::DragFloat("##Rotate", &gizmo.rotateStep, 0.5f, 0.1f, 180.0f, "%.1f\xC2\xB0",
                             ImGuiSliderFlags_AlwaysClamp);
            Widgets::PropertyLabel("Scale");
            ImGui::DragFloat("##Scale", &gizmo.scaleStep, 0.005f, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            Widgets::EndProperties();
        }
        ImGui::EndPopup();
    }
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
    // Details, such as what a build writes, come after the message that matters.
    const std::optional<ConsoleEntry> latest = m_consoleLog->GetLatest(LogLevel::Info);
    if (!latest) {
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
        OpenConsole();
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
                                         .console = "Console",
                                         .mixer = "AudioMixer",
                                         .profiler = "Profiler",
                                         .code = "Code"});
    }
    ImGui::DockSpaceOverViewport(m_dockSpace, ImGui::GetMainViewport());
}

void EditorApplication::DrawEntityMenu(SceneDocument& document) {
    // New entities appear where the view looks.
    const SceneView* view = m_workspace->GetActiveView();
    const Vector3 position = view ? const_cast<SceneView*>(view)->GetCamera().GetFocus() : Vector3{};
    const auto item = [&](const EntityTemplateInfo& info) {
        const std::string label = std::format("{}  {}", info.icon, info.name);
        const bool empty = info.kind == EntityTemplate::Empty;
        if (MenuItem(label.c_str(), empty ? ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N : 0)) {
            CreateEntity(document, info.kind, EntityId::Invalid, position);
        }
    };
    for (const EntityTemplateInfo& info : GetEntityTemplates()) {
        if (!info.category) {
            item(info);
        }
    }
    for (const char* category : {"3D Object", "Light"}) {
        if (ImGui::BeginMenu(category)) {
            for (const EntityTemplateInfo& info : GetEntityTemplates()) {
                if (info.category && std::string_view(info.category) == category) {
                    item(info);
                }
            }
            ImGui::EndMenu();
        }
    }
    ImGui::Separator();
    const bool hasSelection = !document.GetSelection().IsEmpty();
    if (MenuItem(ICON_FA_CLONE "  Duplicate", ImGuiMod_Ctrl | ImGuiKey_D, hasSelection)) {
        DuplicateSelection(document);
    }
    if (MenuItem(ICON_FA_TRASH_CAN "  Delete", ImGuiKey_Delete, hasSelection)) {
        DeleteSelection(document);
    }
}

CommandHistory* EditorApplication::GetFocusedHistory() {
    if (m_codeEditor->IsFocused()) {
        return nullptr;
    }
    if (CommandHistory* history = m_inspector ? m_inspector->GetFocusedHistory() : nullptr) {
        return history;
    }
    SceneDocument* document = m_workspace->GetActiveDocument();
    return document ? &document->GetHistory() : nullptr;
}

void EditorApplication::Undo() {
    if (TextEditor* code = m_codeEditor->IsFocused() ? m_codeEditor->GetActiveEditor() : nullptr) {
        code->Undo();
    } else if (CommandHistory* history = m_inspector ? m_inspector->GetFocusedHistory() : nullptr) {
        history->Undo();
    } else if (SceneDocument* document = m_workspace->GetActiveDocument()) {
        document->Undo();
    }
}

void EditorApplication::Redo() {
    if (TextEditor* code = m_codeEditor->IsFocused() ? m_codeEditor->GetActiveEditor() : nullptr) {
        code->Redo();
    } else if (CommandHistory* history = m_inspector ? m_inspector->GetFocusedHistory() : nullptr) {
        history->Redo();
    } else if (SceneDocument* document = m_workspace->GetActiveDocument()) {
        document->Redo();
    }
}

void EditorApplication::HandleShortcuts() {
    // Global, so they work from any window; a text field being edited takes the keys it uses first.
    constexpr ImGuiInputFlags Global = ImGuiInputFlags_RouteGlobal;
    PlaySession* play = m_workspace->GetPlaySession();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, Global)) {
        TogglePlaying();
        play = m_workspace->GetPlaySession();
    }
    if (play && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, Global)) {
        play->SetPaused(!play->IsPaused());
    }
    if (play && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, Global)) {
        play->Step();
    }
    // While the game's view has focus, the keys are the game's, Escape too; Shift+F1 hands them back to the editor.
    if (play && play->HasFocus()) {
        if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_F1, Global)) {
            ImGui::SetWindowFocus(nullptr);
        }
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_B, Global)) {
        m_context->GetProject()->GetModules().Build(ModuleHost::BuildScope::Game);
    }
    if (ImGui::Shortcut(ImGuiKey_F5, Global)) {
        RunGame();
    }
    if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_F5, Global)) {
        m_runPending = false;
        m_game->Stop();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S, Global)) {
        m_codeEditor->SaveAll();
        m_workspace->SaveAll();
    }
    if (m_codeEditor->IsFocused()) {
        m_codeEditor->HandleShortcuts();
        return;
    }
    SceneDocument* document = m_workspace->GetActiveDocument();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, Global)) {
        m_workspace->NewScene();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, Global)) {
        m_workspace->BrowseForScene();
    }
    // Single keys pick the gizmo's tool, unless they are typed, or move the camera while flying.
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        GizmoSettings& gizmo = m_settings.GetGizmo();
        constexpr std::array Tools{std::pair{ImGuiKey_Q, GizmoTool::Select}, std::pair{ImGuiKey_W, GizmoTool::Move},
                                   std::pair{ImGuiKey_E, GizmoTool::Rotate}, std::pair{ImGuiKey_R, GizmoTool::Scale}};
        for (const auto& [key, tool] : Tools) {
            if (ImGui::Shortcut(key, Global)) {
                gizmo.tool = tool;
            }
        }
        if (ImGui::Shortcut(ImGuiKey_X, Global)) {
            gizmo.local = !gizmo.local;
        }
    }
    if (!document) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, Global | ImGuiInputFlags_Repeat)) {
        Undo();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, Global | ImGuiInputFlags_Repeat) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, Global | ImGuiInputFlags_Repeat)) {
        Redo();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, Global)) {
        m_workspace->Save(*document);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, Global)) {
        m_workspace->SaveAs(*document);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, Global)) {
        m_workspace->Close(*document);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N, Global)) {
        const SceneView* view = m_workspace->GetActiveView();
        CreateEntity(*document, EntityTemplate::Empty, EntityId::Invalid,
                     view ? const_cast<SceneView*>(view)->GetCamera().GetFocus() : Vector3{});
    }
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
    // Game code must be built as the editor was, so this says how.
    const Toolchain toolchain = Toolchain::GetCurrent();
    ImGui::TextColored(EditorColors::TextMuted, "Game code builds with %s, %s", toolchain.GetGeneratorName().c_str(),
                       toolchain.configuration.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Built with Dear ImGui, ImGuizmo and ImGuiColorTextEdit (MIT),\n"
                           "the Inter font and Font Awesome Free\n"
                           "(SIL OFL 1.1; icons CC BY 4.0).");
    ImGui::Spacing();
    const float buttonWidth = ImGui::GetFontSize() * 6.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("Close", {buttonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApplication::DrawOpeningWindow() {
    // Modal, so nothing else is done until the project is open, and without a way to close it: the import ends.
    const bool open = ImGui::IsPopupOpen(OpeningPopupId);
    if (!m_opening && !open) {
        return;
    }
    if (m_opening && !open) {
        ImGui::OpenPopup(OpeningPopupId);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({ImGui::GetFontSize() * 30.0f, 0.0f});
    if (!ImGui::BeginPopupModal(OpeningPopupId, nullptr,
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        return;
    }
    if (!m_opening) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const EditorProject::Opening::Progress progress = m_opening->GetProgress();
    // It turns, so the editor is seen not to hang while a large asset imports.
    const float radius = ImGui::GetFontSize() * 0.55f;
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const ImVec2 center{corner.x + radius, corner.y + ImGui::GetTextLineHeight() * 0.5f + 1.0f};
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float thickness = std::max(2.0f, ImGui::GetFontSize() * 0.13f);
    drawList->AddCircle(center, radius, IM_COL32(255, 255, 255, 40), 32, thickness);
    const auto angle = static_cast<float>(ImGui::GetTime() * 5.0);
    drawList->PathArcTo(center, radius, angle, angle + 5.0f, 32);
    drawList->PathStroke(ImGui::GetColorU32(EditorColors::Accent), thickness);
    ImGui::Dummy({radius * 2.0f, ImGui::GetTextLineHeight()});
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::PushFont(GetBoldFont(), 0.0f);
    const std::string title = std::format("Opening '{}'", m_opening->GetProject().GetSettings().name);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();

    ImGui::Spacing();
    const std::string detail =
        progress.path.empty()
            ? std::string("Looking for changed assets")
            : std::format("Importing {}", Widgets::EllipsizeStart(progress.path, ImGui::GetContentRegionAvail().x -
                                                                                     ImGui::GetFontSize() * 5.0f));
    ImGui::TextColored(EditorColors::TextMuted, "%s", detail.c_str());
    const float fraction =
        progress.count == 0 ? 0.0f : static_cast<float>(progress.index) / static_cast<float>(progress.count);
    const std::string overlay = std::format("{:.0f} s", m_opening->GetDuration().count());
    ImGui::ProgressBar(fraction, {-FLT_MIN, 0.0f}, overlay.c_str());
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
