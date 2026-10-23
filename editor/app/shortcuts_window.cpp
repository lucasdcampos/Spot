#include "app/shortcuts_window.h"

#include "core/text.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"

#include <imgui_stdlib.h>

#include <array>
#include <cfloat>

namespace Spot::Editor {

namespace {

constexpr std::array ScenesShortcuts{
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_N, nullptr, "New scene"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_O, nullptr, "Open a scene"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_S, nullptr, "Save the scene"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, nullptr, "Save the scene as"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S, nullptr, "Save all scenes and scripts"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_W, nullptr, "Close the scene"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_Comma, nullptr, "Preferences"},
    ShortcutInfo{ImGuiKey_F1, nullptr, "Keyboard shortcuts"},
    ShortcutInfo{ImGuiMod_Alt | ImGuiKey_F4, nullptr, "Exit"},
};

constexpr std::array EditingShortcuts{
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_Z, nullptr, "Undo"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_Y, nullptr, "Redo"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, nullptr, "Redo"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_X, nullptr, "Cut"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_C, nullptr, "Copy"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_V, nullptr, "Paste"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_D, nullptr, "Duplicate"},
    ShortcutInfo{ImGuiKey_Delete, nullptr, "Delete"},
    ShortcutInfo{ImGuiKey_F2, nullptr, "Rename"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_A, nullptr, "Select all"},
    ShortcutInfo{ImGuiKey_Escape, nullptr, "Select nothing"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N, nullptr, "Create an empty entity"},
};

constexpr std::array ViewShortcuts{
    ShortcutInfo{ImGuiKey_Q, nullptr, "Select tool"},
    ShortcutInfo{ImGuiKey_W, nullptr, "Move tool"},
    ShortcutInfo{ImGuiKey_E, nullptr, "Rotate tool"},
    ShortcutInfo{ImGuiKey_R, nullptr, "Scale tool"},
    ShortcutInfo{ImGuiKey_X, nullptr, "Switch between the entity's axes and the world's"},
    ShortcutInfo{ImGuiKey_F, nullptr, "Frame the selection"},
    ShortcutInfo{0, "Click", "Select what is under the mouse"},
    ShortcutInfo{0, "Ctrl + Click", "Add to the selection, or take out of it"},
    ShortcutInfo{0, "Ctrl while dragging", "Snap, or stop snapping"},
    ShortcutInfo{0, "Right mouse + W A S D", "Fly; Q and E go down and up, Shift goes faster"},
    ShortcutInfo{0, "Right mouse + wheel", "Change how fast the camera flies"},
    ShortcutInfo{0, "Alt + Left mouse", "Orbit around the pivot"},
    ShortcutInfo{0, "Middle mouse", "Pan; in 2D the right mouse pans too"},
    ShortcutInfo{0, "Mouse wheel", "Zoom"},
};

constexpr std::array PlayShortcuts{
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_P, nullptr, "Play the scene, or stop"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, nullptr, "Pause, or resume"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, nullptr, "Run one frame"},
    ShortcutInfo{ImGuiKey_Escape, nullptr, "Give the keys and the mouse back to the editor, from the game"},
    ShortcutInfo{ImGuiKey_F5, nullptr, "Run the game in its own window"},
    ShortcutInfo{ImGuiMod_Shift | ImGuiKey_F5, nullptr, "Stop the game running in its own window"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_B, nullptr, "Build the game's code"},
};

constexpr std::array AssetShortcuts{
    ShortcutInfo{ImGuiKey_Enter, nullptr, "Open"},
    ShortcutInfo{ImGuiKey_Backspace, nullptr, "Go to the folder above"},
    ShortcutInfo{0, "Arrow keys", "Move the selection"},
    ShortcutInfo{ImGuiKey_F2, nullptr, "Rename"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_D, nullptr, "Duplicate"},
    ShortcutInfo{ImGuiKey_Delete, nullptr, "Delete"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_A, nullptr, "Select all"},
};

constexpr std::array ConsoleShortcuts{
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_C, nullptr, "Copy the selected messages"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_A, nullptr, "Select all messages"},
};

constexpr std::array CodeShortcuts{
    ShortcutInfo{0, "Ctrl + Mouse wheel", "Change the code font size"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_S, nullptr, "Save the script"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_W, nullptr, "Close the script"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_Z, nullptr, "Undo text editing"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_Y, nullptr, "Redo text editing"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, nullptr, "Redo text editing"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_A, nullptr, "Select all text"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_C, nullptr, "Copy text"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_X, nullptr, "Cut text"},
    ShortcutInfo{ImGuiMod_Ctrl | ImGuiKey_V, nullptr, "Paste text"},
    ShortcutInfo{ImGuiKey_Tab, nullptr, "Insert spaces or indent selected lines"},
    ShortcutInfo{ImGuiMod_Shift | ImGuiKey_Tab, nullptr, "Unindent lines"},
    ShortcutInfo{ImGuiKey_Enter, nullptr, "Insert a line with automatic indentation"},
};

constexpr std::array Groups{
    ShortcutGroup{"Scenes and windows", ScenesShortcuts},
    ShortcutGroup{"Editing, in the hierarchy and the scene views", EditingShortcuts},
    ShortcutGroup{"Scene views", ViewShortcuts},
    ShortcutGroup{"Playing", PlayShortcuts},
    ShortcutGroup{"Asset browser", AssetShortcuts},
    ShortcutGroup{"Console", ConsoleShortcuts},
    ShortcutGroup{"Code editor", CodeShortcuts},
};

} // namespace

std::span<const ShortcutGroup> GetShortcutGroups() {
    return Groups;
}

std::string GetShortcutInput(const ShortcutInfo& shortcut) {
    return shortcut.chord ? Widgets::GetKeyChordLabel(shortcut.chord) : std::string(shortcut.input);
}

void ShortcutsWindow::Draw() {
    if (!m_open) {
        return;
    }
    const float fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowSize({fontSize * 34.0f, fontSize * 40.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin(ICON_FA_KEYBOARD "  Keyboard Shortcuts###KeyboardShortcuts", &m_open,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS "  Search shortcuts", &m_search);

    if (ImGui::BeginChild("##Shortcuts")) {
        bool any = false;
        for (const ShortcutGroup& group : Groups) {
            bool headed = false;
            for (const ShortcutInfo& shortcut : group.shortcuts) {
                const std::string input = GetShortcutInput(shortcut);
                if (!m_search.empty() && !ContainsIgnoringCase(shortcut.action, m_search) &&
                    !ContainsIgnoringCase(input, m_search)) {
                    continue;
                }
                if (!headed) {
                    headed = true;
                    ImGui::Spacing();
                    Widgets::Heading(group.name);
                    ImGui::Separator();
                    ImGui::BeginTable(group.name, 2, ImGuiTableFlags_RowBg);
                    ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, fontSize * 11.0f);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(EditorColors::Accent, "%s", input.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(shortcut.action);
            }
            if (headed) {
                ImGui::EndTable();
                any = true;
            }
        }
        if (!any) {
            ImGui::TextColored(EditorColors::TextMuted, "No shortcut matches '%s'.", m_search.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace Spot::Editor
