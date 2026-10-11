#include "panels/console_panel.h"

#include "imgui/editor_style.h"
#include "imgui/icons.h"

#include <imgui_stdlib.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <string_view>
#include <utility>

namespace Spot::Editor {

namespace {

constexpr std::size_t LevelCount = 5;

const char* GetLevelIcon(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
    case LogLevel::Debug:
        return ICON_FA_BUG;
    case LogLevel::Info:
        return ICON_FA_CIRCLE_INFO;
    case LogLevel::Warning:
        return ICON_FA_TRIANGLE_EXCLAMATION;
    case LogLevel::Error:
        return ICON_FA_CIRCLE_XMARK;
    }
    return ICON_FA_CIRCLE_INFO;
}

ImVec4 GetLevelColor(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
    case LogLevel::Debug:
        return EditorColors::TextMuted;
    case LogLevel::Warning:
        return EditorColors::Warning;
    case LogLevel::Error:
        return EditorColors::Error;
    default:
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

std::string_view GetFirstLine(std::string_view message) {
    return message.substr(0, message.find('\n'));
}

std::string FormatTime(std::chrono::system_clock::time_point time) {
    const auto local = std::chrono::current_zone()->to_local(time);
    return std::format("{:%H:%M:%S}", std::chrono::floor<std::chrono::milliseconds>(local));
}

// A button that stays highlighted while its option is on.
bool ToggleButton(const char* label, bool& value, const char* tooltip) {
    if (value) {
        const ImVec4 accent = EditorColors::Accent;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.40f));
    }
    const bool pressed = ImGui::Button(label);
    if (value) {
        ImGui::PopStyleColor(2);
    }
    ImGui::SetItemTooltip("%s", tooltip);
    if (pressed) {
        value = !value;
    }
    return pressed;
}

} // namespace

ConsolePanel::ConsolePanel(std::shared_ptr<ConsoleLog> log)
    : Panel("Console", ICON_FA_TERMINAL "  Console"), m_log(std::move(log)) {
    // Trace messages are noise unless asked for; Debug ones are shown with them by the same toggle.
    m_filter.levels[static_cast<std::size_t>(LogLevel::Trace)] = false;
}

void ConsolePanel::SetFilter(ConsoleFilter filter) {
    m_filter = std::move(filter);
    m_rowsDirty = true;
}

void ConsolePanel::Sync() {
    if (const std::uint64_t clears = m_log->GetClearCount(); clears != m_clearCount) {
        m_clearCount = clears;
        m_entries.clear();
        m_selection.clear();
        m_rowsDirty = true;
    }
    if (m_log->Fetch(m_lastSequence, m_entries) > 0) {
        m_lastSequence = m_entries.back().sequence;
        // Kept as long as the log keeps them, so both drop the same old messages.
        if (m_entries.size() > m_log->GetCapacity()) {
            m_entries.erase(m_entries.begin(),
                            m_entries.begin() + static_cast<std::ptrdiff_t>(m_entries.size() - m_log->GetCapacity()));
        }
        for (const ConsoleEntry& entry : m_entries) {
            if (std::ranges::find(m_sources, entry.source) == m_sources.end()) {
                m_sources.push_back(entry.source);
                std::ranges::sort(m_sources);
            }
        }
        m_rowsDirty = true;
    }
    if (m_rowsDirty) {
        m_rows = BuildConsoleRows(m_entries, m_filter);
        m_rowsDirty = false;
    }
}

void ConsolePanel::OnDraw() {
    Sync();
    DrawToolbar();
    ImGui::Separator();
    DrawCommandLine();
    DrawRows();
}

void ConsolePanel::ExecuteCommand(std::string_view line) {
    m_commandInput.SetLine(std::string(line));
    m_commandInput.Submit();
    m_commandText.clear();
    Sync();
}

int ConsolePanel::OnCommandInput(ImGuiInputTextCallbackData* data) {
    auto& panel = *static_cast<ConsolePanel*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
        panel.m_commandInput.SetLine(data->Buf);
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        panel.m_commandInput.SetLine(data->Buf);
        panel.m_commandInput.Complete();
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        panel.m_commandInput.BrowseHistory(data->EventKey == ImGuiKey_UpArrow ? -1 : 1);
    }
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, panel.m_commandInput.GetLine().c_str());
    return 0;
}

void ConsolePanel::DrawCommandLine() {
    ImGui::SetNextItemWidth(-1.0f);
    constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory |
                                          ImGuiInputTextFlags_CallbackEdit;
    if (ImGui::InputTextWithHint("##Command", "Command (Tab completes; Up/Down browse history)", &m_commandText, flags,
                                 &ConsolePanel::OnCommandInput, this)) {
        ExecuteCommand(m_commandText);
        ImGui::SetKeyboardFocusHere(-1);
    }
}

void ConsolePanel::DrawToolbar() {
    if (ImGui::Button(ICON_FA_TRASH_CAN " Clear")) {
        Clear();
    }
    ImGui::SameLine();
    if (ToggleButton(ICON_FA_LAYER_GROUP " Collapse", m_filter.collapse, "Show identical messages once, counted")) {
        m_rowsDirty = true;
    }

    // Debug and Trace share a toggle; the others have one each, with how many messages they have.
    struct LevelToggle {
        LogLevel level;
        std::size_t count;
        const char* tooltip;
        std::string label;
    };
    const std::array<std::size_t, 5> counts = m_log->GetCounts();
    std::array<LevelToggle, 4> toggles{{
        {LogLevel::Debug, counts[0] + counts[1], "Debug messages", {}},
        {LogLevel::Info, counts[2], "Information", {}},
        {LogLevel::Warning, counts[3], "Warnings", {}},
        {LogLevel::Error, counts[4], "Errors", {}},
    }};
    const ImGuiStyle& style = ImGui::GetStyle();
    float togglesWidth = 0.0f;
    for (LevelToggle& toggle : toggles) {
        toggle.label = toggle.count > 999 ? std::format("{} 999+", GetLevelIcon(toggle.level))
                                          : std::format("{} {}", GetLevelIcon(toggle.level), toggle.count);
        togglesWidth += ImGui::CalcTextSize(toggle.label.c_str()).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
    }

    // The search box takes whatever the source filter and the toggles leave.
    ImGui::SameLine();
    const float sourceWidth = ImGui::GetFontSize() * 8.0f;
    const float searchWidth = ImGui::GetContentRegionAvail().x - togglesWidth - sourceWidth - style.ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(searchWidth, ImGui::GetFontSize() * 6.0f));
    if (ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search", &m_filter.text)) {
        m_rowsDirty = true;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(sourceWidth);
    const char* sourceLabel = m_filter.source.empty() ? "All sources" : m_filter.source.c_str();
    if (ImGui::BeginCombo("##Source", sourceLabel)) {
        if (ImGui::Selectable("All sources", m_filter.source.empty())) {
            m_filter.source.clear();
            m_rowsDirty = true;
        }
        for (const std::string& source : m_sources) {
            if (ImGui::Selectable(source.c_str(), m_filter.source == source)) {
                m_filter.source = source;
                m_rowsDirty = true;
            }
        }
        ImGui::EndCombo();
    }

    for (const LevelToggle& toggle : toggles) {
        ImGui::SameLine();
        bool& shown = m_filter.levels[static_cast<std::size_t>(toggle.level)];
        ImGui::PushID(static_cast<int>(toggle.level));
        ImGui::PushStyleColor(ImGuiCol_Text, shown ? GetLevelColor(toggle.level) : EditorColors::TextMuted);
        if (ToggleButton(toggle.label.c_str(), shown, toggle.tooltip)) {
            if (toggle.level == LogLevel::Debug) {
                m_filter.levels[static_cast<std::size_t>(LogLevel::Trace)] = shown;
            }
            m_rowsDirty = true;
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void ConsolePanel::DrawRows() {
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {ImGui::GetStyle().CellPadding.x, 2.0f});
    if (!ImGui::BeginTable("##Messages", 5, flags)) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed);

    // New messages keep the view at the bottom only if it was already there.
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto rowIndex = static_cast<std::size_t>(i);
            const ConsoleRow& row = m_rows[rowIndex];
            const ConsoleEntry& entry = m_entries[row.entry];
            ImGui::PushID(static_cast<int>(entry.sequence));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const bool selected = m_selection.contains(entry.sequence);
            if (ImGui::Selectable("##Row", selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                Select(rowIndex);
            }
            if (ImGui::BeginPopupContextItem("##RowMenu")) {
                if (!selected) {
                    Select(rowIndex);
                }
                DrawRowContextMenu(entry);
                ImGui::EndPopup();
            }
            const std::string_view firstLine = GetFirstLine(entry.message);
            if (firstLine.size() != entry.message.size() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::SetTooltip("%s", entry.message.c_str());
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(GetLevelColor(entry.level), "%s", GetLevelIcon(entry.level));

            ImGui::TableNextColumn();
            ImGui::TextColored(EditorColors::TextMuted, "%s", FormatTime(entry.time).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(EditorColors::TextMuted, "%s", entry.source.c_str());
            ImGui::TableNextColumn();
            if (entry.level >= LogLevel::Warning) {
                ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(entry.level));
            }
            ImGui::TextUnformatted(firstLine.data(), firstLine.data() + firstLine.size());
            if (entry.level >= LogLevel::Warning) {
                ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            if (row.count > 1) {
                ImGui::TextColored(EditorColors::TextMuted, "%zu", row.count);
            }
            ImGui::PopID();
        }
    }

    if (ImGui::IsWindowFocused()) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
            CopySelection();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
            for (const ConsoleRow& row : m_rows) {
                m_selection.insert(m_entries[row.entry].sequence);
            }
        }
        if (ImGui::Shortcut(ImGuiKey_Escape)) {
            m_selection.clear();
        }
    }

    if (m_scrollToBottom || (atBottom && ImGui::GetScrollMaxY() > ImGui::GetScrollY())) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void ConsolePanel::DrawRowContextMenu(const ConsoleEntry& entry) {
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy", ImGui::GetKeyChordName(ImGuiMod_Ctrl | ImGuiKey_C))) {
        CopySelection();
    }
    if (ImGui::MenuItem("Copy Message")) {
        ImGui::SetClipboardText(entry.message.c_str());
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Clear")) {
        Clear();
    }
}

void ConsolePanel::Select(std::size_t row) {
    const std::uint64_t sequence = m_entries[m_rows[row].entry].sequence;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && m_selectionAnchor != 0) {
        const auto anchor = std::ranges::find_if(m_rows, [&](const ConsoleRow& candidate) {
            return m_entries[candidate.entry].sequence == m_selectionAnchor;
        });
        if (anchor != m_rows.end()) {
            const std::size_t from = std::min(row, static_cast<std::size_t>(anchor - m_rows.begin()));
            const std::size_t to = std::max(row, static_cast<std::size_t>(anchor - m_rows.begin()));
            if (!io.KeyCtrl) {
                m_selection.clear();
            }
            for (std::size_t i = from; i <= to; ++i) {
                m_selection.insert(m_entries[m_rows[i].entry].sequence);
            }
            return;
        }
    }
    if (io.KeyCtrl) {
        if (!m_selection.erase(sequence)) {
            m_selection.insert(sequence);
        }
    } else {
        m_selection = {sequence};
    }
    m_selectionAnchor = sequence;
}

void ConsolePanel::CopySelection() const {
    std::string text;
    for (const ConsoleRow& row : m_rows) {
        const ConsoleEntry& entry = m_entries[row.entry];
        if (m_selection.contains(entry.sequence)) {
            text += FormatConsoleEntry(entry);
            if (row.count > 1) {
                text += std::format(" (x{})", row.count);
            }
            text += '\n';
        }
    }
    if (!text.empty()) {
        ImGui::SetClipboardText(text.c_str());
    }
}

void ConsolePanel::Clear() {
    m_log->Clear();
    Sync();
}

} // namespace Spot::Editor
