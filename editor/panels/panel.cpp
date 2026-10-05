#include "panels/panel.h"

#include "imgui/editor_style.h"

#include <algorithm>

namespace Spot::Editor {

Panel::Panel(std::string_view id, std::string_view title)
    : m_id(id), m_title(title), m_windowName(m_title + "###" + m_id) {
}

void Panel::Draw() {
    if (!m_open) {
        return;
    }
    if (ImGui::Begin(m_windowName.c_str(), &m_open, GetWindowFlags())) {
        OnDraw();
    }
    ImGui::End();
}

void Panel::DrawEmptyState(const char* icon, std::string_view text) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float wrapWidth = std::max(available.x - ImGui::GetStyle().WindowPadding.x * 2.0f, 1.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(text.data(), text.data() + text.size(), false, wrapWidth);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    ImGui::PopFont();
    const float height = iconSize.y + ImGui::GetStyle().ItemSpacing.y + textSize.y;
    const ImVec2 start = ImGui::GetCursorPos();

    ImGui::PushStyleColor(ImGuiCol_Text, EditorColors::TextMuted);
    ImGui::SetCursorPos({start.x + std::max((available.x - iconSize.x) * 0.5f, 0.0f),
                         start.y + std::max((available.y - height) * 0.5f, 0.0f)});
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
    ImGui::TextUnformatted(icon);
    ImGui::PopFont();
    ImGui::SetCursorPosX(start.x + std::max((available.x - textSize.x) * 0.5f, 0.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

} // namespace Spot::Editor
