#include "panels/code_editor_panel.h"

#include "app/editor_context.h"
#include "code/script_editor.h"
#include "core/editor_log.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "play/play_session.h"
#include "scene/scene_workspace.h"

#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>

namespace Spot::Editor {

namespace {

constexpr const char* SavePrompt = "Save script changes?###CodeSavePrompt";
constexpr std::string_view Utf8Bom = "\xef\xbb\xbf";

std::optional<std::string> ReadSource(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return stream.bad() ? std::nullopt : std::optional(std::move(text));
}

bool WriteSource(const std::filesystem::path& file, std::string_view text) {
    const std::string suffix = ".spot-" + AssetId::Generate().ToString();
    const std::filesystem::path temporary =
        file.parent_path() / (file.filename().native() + Utf8ToPath(suffix + ".tmp").native());
    const std::filesystem::path backup =
        file.parent_path() / (file.filename().native() + Utf8ToPath(suffix + ".bak").native());
    std::ofstream stream(temporary, std::ios::binary);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    std::error_code error;
    if (!stream) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    std::filesystem::rename(temporary, file, error);
    if (!error) {
        return true;
    }
    // Windows cannot rename over an existing file. Keep a backup until its replacement succeeds.
    std::filesystem::rename(file, backup, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::rename(backup, file, error);
        std::filesystem::remove(temporary, error);
        return false;
    }
    std::filesystem::remove(backup, error);
    return true;
}

} // namespace

struct CodeEditorPanel::Document {
    AssetId asset;
    std::string path;
    TextEditor editor;
    std::string savedText;
    std::string diskText;
    bool crlf = false;
    bool bom = false;
    bool missing = false;
    bool conflict = false;
    // The last checked text and its first syntax error, rechecked shortly after typing stops.
    std::string checkedText;
    std::optional<ScriptDiagnostic> diagnostic;
    double changedAt = -1;

    bool IsDirty() const { return GetEditorText(editor) != savedText; }

    void Load(std::string text) {
        diskText = std::move(text);
        bom = diskText.starts_with(Utf8Bom);
        crlf = diskText.find("\r\n") != std::string::npos;
        // Space insertion applies to newly typed tabs, not to the file's existing indentation.
        editor.SetInsertSpacesOnTabs(false);
        editor.SetText(std::string_view(diskText).substr(bom ? Utf8Bom.size() : 0));
        editor.SetInsertSpacesOnTabs(true);
        savedText = GetEditorText(editor);
        missing = false;
        conflict = false;
    }
};

CodeEditorPanel::CodeEditorPanel(EditorContext& context)
    : Panel("Code", ICON_FA_FILE_CODE "  Code"), m_context(context), m_fontSize(BaseFontSize) {
    SetOpen(false);
    // Breaks come in the middle of the game's frame: keep the first until Continue and pause the game after it.
    SetScriptBreakHandler([this](const ScriptBreak& stop) {
        if (m_break || stop.frames.empty())
            return;
        m_break = stop;
        m_showBreak = true;
        if (SceneWorkspace* workspace = m_context.GetWorkspace(); workspace && workspace->GetPlaySession())
            workspace->GetPlaySession()->SetPaused(true);
    });
}

CodeEditorPanel::~CodeEditorPanel() {
    SetScriptBreakHandler({});
}

CodeEditorPanel::Document* CodeEditorPanel::Find(AssetId asset) const {
    const auto found =
        std::ranges::find_if(m_documents, [asset](const auto& document) { return document->asset == asset; });
    return found == m_documents.end() ? nullptr : found->get();
}

std::string CodeEditorPanel::GetActivePath() const {
    const Document* document = Find(m_active);
    return document ? document->path : std::string();
}

TextEditor* CodeEditorPanel::GetActiveEditor() {
    Document* document = Find(m_active);
    return document ? &document->editor : nullptr;
}

bool CodeEditorPanel::HasUnsavedChanges() const {
    return std::ranges::any_of(m_documents, [](const auto& document) { return document->IsDirty(); });
}

bool CodeEditorPanel::IsFocused() const {
    if (!IsOpen()) {
        return false;
    }
    const ImGuiWindow* window = ImGui::FindWindowByName(GetWindowName().c_str());
    const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
    // Menus temporarily have focus; their actions still apply to the document that opened them.
    if (focused && (focused->Flags & ImGuiWindowFlags_Popup)) {
        return m_wasFocused;
    }
    return window && focused && focused->RootWindow == window->RootWindow;
}

bool CodeEditorPanel::Fail(std::string message) {
    m_error = std::move(message);
    GetEditorLogger().Error("{}", m_error);
    return false;
}

bool CodeEditorPanel::OpenScript(AssetId asset) {
    EditorProject* project = m_context.GetProject();
    const AssetInfo* info = project ? project->GetAssetDatabase().Find(asset) : nullptr;
    if (!info || info->type != AssetType::Script) {
        return Fail("Only a script asset in the current project can be opened in the code editor.");
    }
    if (!Find(asset)) {
        auto source = ReadSource(project->GetProject().GetAssetsDirectory() / Utf8ToPath(info->path));
        if (!source) {
            return Fail(std::format("Could not read script '{}'.", info->path));
        }
        auto document = std::make_unique<Document>();
        document->asset = asset;
        document->path = info->path;
        ConfigureScriptEditor(document->editor, info->path);
        document->Load(std::move(*source));
        m_documents.push_back(std::move(document));
    }
    m_active = asset;
    m_select = asset;
    m_focus = true;
    m_error.clear();
    SetOpen(true);
    return true;
}

void CodeEditorPanel::SyncWithAssets() {
    EditorProject* project = m_context.GetProject();
    if (!project || project->GetVersion() == m_assetsVersion) {
        return;
    }
    m_assetsVersion = project->GetVersion();
    for (auto& document : m_documents) {
        const AssetInfo* info = project->GetAssetDatabase().Find(document->asset);
        document->missing = !info || info->type != AssetType::Script;
        if (document->missing) {
            continue;
        }
        if (document->path != info->path) {
            document->path = info->path;
            ConfigureScriptEditor(document->editor, info->path);
        }
        auto source = ReadSource(project->GetProject().GetAssetsDirectory() / Utf8ToPath(info->path));
        if (!source) {
            document->missing = true;
        } else if (*source != document->diskText) {
            if (document->IsDirty()) {
                document->conflict = true;
            } else {
                document->Load(std::move(*source));
            }
        } else {
            document->conflict = false;
        }
    }
}

void CodeEditorPanel::UpdateDiagnostic(Document& document) {
    // Parsing is cheap but not free: wait until typing pauses before checking again.
    constexpr double Delay = 0.3;
    const auto text = GetEditorText(document.editor);
    if (text == document.checkedText)
        return;
    const double now = ImGui::GetTime();
    if (document.changedAt < 0)
        document.changedAt = now;
    if (now - document.changedAt < Delay && !document.checkedText.empty())
        return;
    document.changedAt = -1;
    document.checkedText = text;
    document.diagnostic.reset();
    try {
        document.diagnostic = CheckScriptSyntax(GetScriptLanguageForSource(document.path), text, document.path);
    } catch (const std::exception&) {
        // Sources of unregistered languages are edited without checks.
    }
    RefreshMarkers(document);
}

void CodeEditorPanel::RefreshMarkers(Document& document) {
    document.editor.ClearMarkers();
    if (const auto found = m_breakpoints.find(document.path); found != m_breakpoints.end()) {
        for (const auto line : found->second)
            document.editor.AddMarker(line - 1, ImGui::GetColorU32(ImVec4{0.85f, 0.2f, 0.2f, 1.0f}), 0, "Breakpoint",
                                      {});
    }
    if (m_break && m_break->frames.front().sourceName == document.path) {
        document.editor.AddMarker(m_break->frames.front().line - 1, ImGui::GetColorU32(EditorColors::Warning),
                                  ImGui::GetColorU32(ImVec4{0.96f, 0.72f, 0.26f, 0.22f}), "Paused here", {});
    }
    if (document.diagnostic && document.diagnostic->line > 0) {
        const auto& diagnostic = *document.diagnostic;
        document.editor.AddMarker(diagnostic.line - 1, ImGui::GetColorU32(EditorColors::Error),
                                  ImGui::GetColorU32(ImVec4{0.9f, 0.25f, 0.2f, 0.18f}), diagnostic.message,
                                  diagnostic.message);
    }
}

void CodeEditorPanel::ToggleBreakpoint(Document& document, std::size_t line) {
    auto& lines = m_breakpoints[document.path];
    if (!lines.erase(line))
        lines.insert(line);
    SetScriptBreakpoints(document.path, {lines.begin(), lines.end()});
    if (lines.empty())
        m_breakpoints.erase(document.path);
    RefreshMarkers(document);
}

void CodeEditorPanel::DrawBreak() {
    if (!m_break)
        return;
    const auto& top = m_break->frames.front();
    if (m_showBreak) {
        // Show the script it stopped in, at its line, once.
        m_showBreak = false;
        if (EditorProject* project = m_context.GetProject()) {
            if (const auto asset = project->GetAssetDatabase().FindByPath(top.sourceName);
                asset && OpenScript(*asset)) {
                if (Document* document = Find(*asset)) {
                    document->editor.SetCursor({top.line - 1, 0});
                    document->editor.ScrollToLine(top.line - 1);
                    RefreshMarkers(*document);
                }
            }
        }
    }
    ImGui::TextColored(EditorColors::Warning, "Paused at %s (%s:%zu)", top.function.c_str(), top.sourceName.c_str(),
                       top.line);
    ImGui::SameLine();
    const bool resume = ImGui::Button("Continue");
    RecordItem("Continue");
    SceneWorkspace* workspace = m_context.GetWorkspace();
    PlaySession* play = workspace ? workspace->GetPlaySession() : nullptr;
    if (play) {
        ImGui::SameLine();
        // The game runs one more frame, stopping again at the next breakpoint it reaches.
        if (ImGui::Button("Next Frame")) {
            m_break.reset();
            play->Step();
            return;
        }
    }
    for (std::size_t i = 0; i < m_break->frames.size(); ++i) {
        const auto& frame = m_break->frames[i];
        const auto label = std::format("{} ({}:{})##frame{}", frame.function, frame.sourceName, frame.line, i);
        if (ImGui::TreeNodeEx(label.c_str(), i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) {
            for (const auto& [name, value] : frame.locals)
                ImGui::BulletText("%s = %s", name.c_str(), value.c_str());
            if (frame.locals.empty())
                ImGui::TextDisabled("No locals");
            ImGui::TreePop();
        }
    }
    ImGui::Separator();
    if (resume) {
        const auto path = top.sourceName;
        m_break.reset();
        if (play)
            play->SetPaused(false);
        for (auto& document : m_documents) {
            if (document->path == path)
                RefreshMarkers(*document);
        }
    }
}

bool CodeEditorPanel::Save(Document& document) {
    EditorProject* project = m_context.GetProject();
    const AssetInfo* info = project ? project->GetAssetDatabase().Find(document.asset) : nullptr;
    if (!info || info->type != AssetType::Script) {
        return Fail(
            std::format("Script '{}' was removed. Its unsaved text is still available to copy.", document.path));
    }
    document.path = info->path;
    const auto file = project->GetProject().GetAssetsDirectory() / Utf8ToPath(document.path);
    const auto disk = ReadSource(file);
    if (!disk) {
        return Fail(std::format("Could not read script '{}' before saving. Its text has been kept in the editor.",
                                document.path));
    }
    if (*disk != document.diskText) {
        document.conflict = true;
        return Fail(std::format("Script '{}' changed on disk. Copy your changes or reload the file before saving.",
                                document.path));
    }
    const std::string text = GetEditorText(document.editor);
    if (text == document.savedText) {
        return true;
    }
    std::string encoded = document.bom ? std::string(Utf8Bom) : std::string();
    for (const char character : text) {
        if (character == '\n' && document.crlf) {
            encoded += '\r';
        }
        encoded += character;
    }
    if (!WriteSource(file, encoded)) {
        return Fail(std::format("Could not save script '{}'. Its text has been kept in the editor.", document.path));
    }
    document.savedText = text;
    document.diskText = std::move(encoded);
    document.conflict = false;
    m_error.clear();
    project->Refresh();
    GetEditorLogger().Info("Saved script '{}'", document.path);
    return true;
}

bool CodeEditorPanel::SaveActive() {
    SyncWithAssets();
    Document* document = Find(m_active);
    return document && Save(*document);
}

bool CodeEditorPanel::SaveAll() {
    SyncWithAssets();
    bool saved = true;
    for (auto& document : m_documents) {
        if (document->IsDirty() && !Save(*document)) {
            saved = false;
        }
    }
    return saved;
}

bool CodeEditorPanel::Reload(Document& document) {
    EditorProject* project = m_context.GetProject();
    const AssetInfo* info = project ? project->GetAssetDatabase().Find(document.asset) : nullptr;
    const auto source =
        info ? ReadSource(project->GetProject().GetAssetsDirectory() / Utf8ToPath(info->path)) : std::nullopt;
    if (!source) {
        return Fail(std::format("Could not reload script '{}'. Its text has been kept in the editor.", document.path));
    }
    if (document.path != info->path) {
        document.path = info->path;
        ConfigureScriptEditor(document.editor, info->path);
    }
    document.Load(*source);
    document.editor.SetFocus();
    m_error.clear();
    project->Refresh();
    return true;
}

void CodeEditorPanel::Remove(AssetId asset) {
    std::erase_if(m_documents, [asset](const auto& document) { return document->asset == asset; });
    if (m_active == asset) {
        m_active = m_documents.empty() ? AssetId() : m_documents.back()->asset;
        m_select = m_active;
    }
}

void CodeEditorPanel::CloseActive() {
    if (Find(m_active)) {
        const AssetId asset = m_active;
        Confirm({asset}, [this, asset] { Remove(asset); });
    }
}

void CodeEditorPanel::CloseAll() {
    m_documents.clear();
    m_active = {};
    m_select = {};
    m_prompted.clear();
    m_afterPrompt = {};
    m_openPrompt = false;
    m_wasFocused = false;
    m_error.clear();
    m_assetsVersion = 0;
    SetOpen(false);
}

void CodeEditorPanel::Confirm(std::vector<AssetId> documents, std::function<void()> proceed) {
    if (m_afterPrompt) {
        return;
    }
    std::erase_if(documents, [this](AssetId asset) {
        const Document* document = Find(asset);
        return !document || !document->IsDirty();
    });
    if (documents.empty()) {
        proceed();
        return;
    }
    m_prompted = std::move(documents);
    m_afterPrompt = std::move(proceed);
    m_openPrompt = true;
    m_focus = true;
    SetOpen(true);
}

void CodeEditorPanel::ConfirmAllUnsavedChanges(std::function<void()> proceed) {
    std::vector<AssetId> documents;
    for (const auto& document : m_documents) {
        documents.push_back(document->asset);
    }
    Confirm(std::move(documents), std::move(proceed));
}

void CodeEditorPanel::HandleShortcuts() {
    if (m_afterPrompt || !IsFocused() || !Find(m_active)) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        SaveActive();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, ImGuiInputFlags_RouteGlobal)) {
        CloseActive();
    }
    if (ImGui::Shortcut(ImGuiKey_F9, ImGuiInputFlags_RouteGlobal)) {
        Document* document = Find(m_active);
        ToggleBreakpoint(*document, document->editor.GetMainCursorPosition().line + 1);
    }
}

std::optional<std::pair<ImVec2, ImVec2>> CodeEditorPanel::GetItemRect(std::string_view name) const {
    const auto item = m_rects.find(std::string(name));
    return item == m_rects.end() ? std::nullopt : std::optional(item->second);
}

void CodeEditorPanel::RecordItem(const char* name) {
    m_rects[name] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
}

void CodeEditorPanel::SetUpWindow() {
    ImGui::SetNextWindowSize({760, 500}, ImGuiCond_FirstUseEver);
    if (m_focus) {
        ImGui::SetNextWindowFocus();
    }
}

void CodeEditorPanel::OnDraw() {
    SyncWithAssets();
    if (const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
        !focused || !(focused->Flags & ImGuiWindowFlags_Popup)) {
        m_wasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    }
    m_rects.clear();
    DrawBreak();
    if (m_documents.empty()) {
        DrawEmptyState(ICON_FA_FILE_CODE, "Open a script asset to edit its source.");
    } else {
        std::vector<AssetId> close;
        if (ImGui::BeginTabBar("##Scripts", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
                                                ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (auto& document : m_documents) {
                const bool dirty = document->IsDirty();
                const std::string label =
                    PathToUtf8(Utf8ToPath(document->path).filename()) + "###" + document->asset.ToString();
                bool open = true;
                ImGuiTabItemFlags flags = dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                if (m_select == document->asset) {
                    flags |= ImGuiTabItemFlags_SetSelected;
                }
                if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                    if (m_active != document->asset) {
                        document->editor.SetFocus();
                    }
                    m_active = document->asset;
                    if (m_select == document->asset) {
                        m_select = {};
                    }
                    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save")) {
                        Save(*document);
                        document->editor.SetFocus();
                    }
                    RecordItem("Save");
                    ImGui::SameLine();
                    if (ImGui::Button("Reload from Disk")) {
                        const AssetId asset = document->asset;
                        Confirm({asset}, [this, asset] {
                            if (Document* found = Find(asset))
                                Reload(*found);
                        });
                    }
                    RecordItem("Reload");
                    ImGui::SameLine();
                    if (ImGui::Button("Breakpoint"))
                        ToggleBreakpoint(*document, document->editor.GetMainCursorPosition().line + 1);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Stops scripts at the cursor's line while playing (F9)");
                    RecordItem("Breakpoint");
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s%s", document->path.c_str(), dirty ? " *" : "");
                    if (document->missing || document->conflict) {
                        ImGui::TextColored(EditorColors::Warning, "%s",
                                           document->missing
                                               ? "File removed. Unsaved text is kept here to copy."
                                               : "File changed on disk. Copy your changes or reload before saving.");
                    }
                    if (!m_error.empty()) {
                        ImGui::TextWrapped("%s", m_error.c_str());
                    }
                    const float statusHeight = ImGui::GetTextLineHeightWithSpacing();
                    const ImVec2 size{0, std::max(1.0f, ImGui::GetContentRegionAvail().y - statusHeight)};
                    if (m_focus) {
                        document->editor.SetFocus();
                        m_focus = false;
                    }
                    const ImGuiIO& io = ImGui::GetIO();
                    const ImVec2 sourceMin = ImGui::GetCursorScreenPos();
                    const ImVec2 sourceMax{sourceMin.x + ImGui::GetContentRegionAvail().x, sourceMin.y + size.y};
                    if (io.KeyCtrl && io.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                        ImGui::IsMouseHoveringRect(sourceMin, sourceMax)) {
                        m_fontSize = std::clamp(m_fontSize + io.MouseWheel, 10.0f, 40.0f);
                        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetID("##FontZoom"));
                    }
                    UpdateDiagnostic(*document);
                    ImGui::PushFont(GetCodeFont(), m_fontSize);
                    document->editor.Render("##Source", size);
                    RecordItem("Source");
                    ImGui::PopFont();
                    const auto position = document->editor.GetMainCursorPosition();
                    ImGui::TextDisabled("Ln %zu, Col %zu  |  %s  |  Spaces: 4", position.line + 1, position.index + 1,
                                        document->editor.GetLanguageName().c_str());
                    if (document->diagnostic) {
                        ImGui::SameLine();
                        const auto& diagnostic = *document->diagnostic;
                        ImGui::TextColored(EditorColors::Error, "  %s",
                                           diagnostic.line
                                               ? std::format("Line {}: {}", diagnostic.line, diagnostic.message).c_str()
                                               : diagnostic.message.c_str());
                        RecordItem("Diagnostic");
                    }
                    ImGui::EndTabItem();
                }
                if (!open) {
                    close.push_back(document->asset);
                }
            }
            ImGui::EndTabBar();
        }
        if (!close.empty()) {
            Confirm(close, [this, close] {
                for (AssetId asset : close)
                    Remove(asset);
            });
        }
    }
    DrawSavePrompt();
}

void CodeEditorPanel::DrawSavePrompt() {
    if (m_openPrompt) {
        ImGui::OpenPopup(SavePrompt);
        m_openPrompt = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal(SavePrompt, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TextUnformatted("Save the changes to these scripts?");
    for (AssetId asset : m_prompted) {
        if (const Document* document = Find(asset)) {
            ImGui::BulletText("%s", document->path.c_str());
        }
    }
    if (!m_error.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38);
        ImGui::TextColored(EditorColors::Warning, "%s", m_error.c_str());
        ImGui::PopTextWrapPos();
    }
    const bool save = ImGui::Button("Save", {100, 0});
    RecordItem("SavePrompt/Save");
    ImGui::SameLine();
    const bool discard = ImGui::Button("Don't Save", {100, 0});
    RecordItem("SavePrompt/Discard");
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", {100, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    RecordItem("SavePrompt/Cancel");
    bool saved = save;
    if (save) {
        SyncWithAssets();
        for (AssetId asset : m_prompted) {
            if (Document* document = Find(asset); document && !Save(*document)) {
                saved = false;
                break;
            }
        }
    }
    std::function<void()> proceed;
    if (saved || discard || cancel) {
        ImGui::CloseCurrentPopup();
        proceed = std::exchange(m_afterPrompt, {});
        m_prompted.clear();
        if (cancel) {
            proceed = {};
        }
    }
    ImGui::EndPopup();
    if (proceed) {
        proceed();
    }
}

} // namespace Spot::Editor
