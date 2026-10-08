#pragma once

#include <imgui.h>

#include <span>
#include <string>

namespace Spot::Editor {

// A key or mouse action and what it does.
struct ShortcutInfo {
    // Keys, named as menus name them; none for mouse actions, which the text names.
    ImGuiKeyChord chord = 0;
    const char* input = nullptr;
    const char* action = nullptr;
};

struct ShortcutGroup {
    const char* name;
    std::span<const ShortcutInfo> shortcuts;
};

// Every shortcut of the editor, by where it works.
std::span<const ShortcutGroup> GetShortcutGroups();
// How the shortcut is pressed, as "Ctrl+Shift+S" or "Right mouse + W A S D".
std::string GetShortcutInput(const ShortcutInfo& shortcut);

// Lists the editor's shortcuts, with a search.
class ShortcutsWindow {
public:
    void Open() { m_open = true; }
    bool IsOpen() const { return m_open; }
    void Draw();

private:
    bool m_open = false;
    std::string m_search;
};

} // namespace Spot::Editor
