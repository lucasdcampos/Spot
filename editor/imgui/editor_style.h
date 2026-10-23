#pragma once

#include <imgui.h>

namespace Spot::Editor {

// Font size at a content scale of 1, in screen coordinates.
inline constexpr float BaseFontSize = 14.0f;

// Colors shared by panels that draw their own highlights. They are as displayed, like every ImGui color.
namespace EditorColors {
inline constexpr ImVec4 Accent{0.239f, 0.545f, 0.992f, 1.0f};
inline constexpr ImVec4 Warning{0.957f, 0.722f, 0.259f, 1.0f};
inline constexpr ImVec4 Error{0.957f, 0.341f, 0.341f, 1.0f};
inline constexpr ImVec4 Success{0.400f, 0.784f, 0.459f, 1.0f};
inline constexpr ImVec4 TextMuted{0.553f, 0.573f, 0.612f, 1.0f};
} // namespace EditorColors

// Loads Inter with Font Awesome icons merged in, as the regular and the bold font, into the current context's atlas.
void LoadEditorFonts();
// The semibold font, for headers and emphasis. Pushed with ImGui::PushFont(GetBoldFont(), 0.0f).
ImFont* GetBoldFont();
// The embedded monospaced font used for source code; null in contexts without editor fonts.
ImFont* GetCodeFont();

// Replaces the current context's style with the editor's theme, sized for the content scale.
void ApplyEditorStyle(float contentScale);

} // namespace Spot::Editor
