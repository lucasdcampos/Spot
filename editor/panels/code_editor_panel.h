#pragma once

#include "panels/panel.h"

#include <spot.h>
#include <spot/scene/scripting/script_runtime.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class TextEditor;

namespace Spot::Editor {

class EditorContext;

// Source documents belong to the editor, independently of the runtime and the scene undo history.
class CodeEditorPanel final : public Panel {
public:
    explicit CodeEditorPanel(EditorContext& context);
    ~CodeEditorPanel() override;

    bool OpenScript(AssetId asset);
    std::size_t GetDocumentCount() const { return m_documents.size(); }
    AssetId GetActiveAsset() const { return m_active; }
    std::string GetActivePath() const;
    TextEditor* GetActiveEditor();
    bool HasUnsavedChanges() const;
    bool IsFocused() const;

    bool SaveActive();
    bool SaveAll();
    void CloseActive();
    void CloseAll();
    void ConfirmAllUnsavedChanges(std::function<void()> proceed);
    void SyncWithAssets();
    // Document shortcuts are handled before the application's global scene shortcuts.
    void HandleShortcuts();
    const std::string& GetError() const { return m_error; }
    std::optional<std::pair<ImVec2, ImVec2>> GetItemRect(std::string_view name) const;

protected:
    void OnDraw() override;
    void SetUpWindow() override;
    ImGuiWindowFlags GetWindowFlags() const override {
        return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
    }

private:
    struct Document;
    Document* Find(AssetId asset) const;
    bool Save(Document& document);
    void UpdateDiagnostic(Document& document);
    void RefreshMarkers(Document& document);
    void ToggleBreakpoint(Document& document, std::size_t line);
    void DrawBreak();
    bool Reload(Document& document);
    void Remove(AssetId asset);
    void Confirm(std::vector<AssetId> documents, std::function<void()> proceed);
    void DrawSavePrompt();
    void RecordItem(const char* name);
    bool Fail(std::string message);

    EditorContext& m_context;
    std::vector<std::unique_ptr<Document>> m_documents;
    AssetId m_active;
    AssetId m_select;
    float m_fontSize;
    // Breakpoint lines by script path, and the stop the debugger reported, until Continue.
    std::map<std::string, std::set<std::size_t>> m_breakpoints;
    std::optional<ScriptBreak> m_break;
    bool m_showBreak = false;
    bool m_focus = false;
    bool m_wasFocused = false;
    std::uint64_t m_assetsVersion = 0;
    std::string m_error;
    std::vector<AssetId> m_prompted;
    std::function<void()> m_afterPrompt;
    bool m_openPrompt = false;
    std::unordered_map<std::string, std::pair<ImVec2, ImVec2>> m_rects;
};

} // namespace Spot::Editor
