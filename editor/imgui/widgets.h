#pragma once

#include <imgui.h>

#include <chrono>
#include <string>
#include <string_view>

// Small widgets shared by the editor's windows.
namespace Spot::Editor::Widgets {

// A two-column table of labeled properties: labels on the left, editors filling the right.
bool BeginProperties(const char* id);
void EndProperties();
// Starts a row: draws the label and makes the next widget fill the value column.
void PropertyLabel(const char* label, const char* tooltip = nullptr);

// Text drawn as a header in the bold font.
void Heading(const char* text, float scale = 1.0f);

// Muted "(?)" that explains something when hovered.
void HelpMarker(const char* text);

// The end of the text, after "...", when the whole of it is wider than the width, as paths read best.
std::string EllipsizeStart(std::string_view text, float maxWidth);

// How the keys are pressed, as menus show it: "Ctrl+Shift+S", "Ctrl+,".
std::string GetKeyChordLabel(ImGuiKeyChord chord);

// "just now", "5 minutes ago", "yesterday" or a date for older times.
std::string FormatRelativeTime(std::chrono::system_clock::time_point time,
                               std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

} // namespace Spot::Editor::Widgets
