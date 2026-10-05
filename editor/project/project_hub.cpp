#include "project/project_hub.h"

#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"

#include <spot.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <system_error>
#include <utility>

namespace Spot::Editor {

namespace {

bool ContainsIgnoringCase(std::string_view text, std::string_view part) {
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return part.empty() || !std::ranges::search(text, part, {}, lower, lower).empty();
}

} // namespace

ProjectHub::ProjectHub(EditorSettings& settings, ProjectHubActions actions)
    : m_settings(settings), m_actions(std::move(actions)) {
}

void ProjectHub::Draw() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (!ImGui::Begin("##ProjectHub", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    // One centered column, as wide as reads comfortably; a child window keeps separators inside it.
    const float fontSize = ImGui::GetFontSize();
    const float width = std::min(ImGui::GetContentRegionAvail().x, fontSize * 52.0f);
    const float margin = (ImGui::GetContentRegionAvail().x - width) * 0.5f;
    ImGui::SetCursorPos({ImGui::GetCursorPosX() + margin, ImGui::GetCursorPosY() + fontSize * 3.0f});
    ImGui::BeginChild("##Column", {width, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushItemWidth(width);

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.4f);
    ImGui::TextColored(EditorColors::Accent, ICON_FA_CUBE);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::BeginGroup();
    Widgets::Heading("Spot Editor", 1.6f);
    ImGui::TextColored(EditorColors::TextMuted, "Version %s", GetVersionString().data());
    ImGui::EndGroup();
    ImGui::Dummy({0.0f, fontSize});

    const float buttonHeight = ImGui::GetFrameHeight() * 1.4f;
    ImGui::PushStyleColor(ImGuiCol_Button, EditorColors::Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.61f, 1.0f, 1.0f));
    if (ImGui::Button(ICON_FA_PLUS "  New Project", {fontSize * 11.0f, buttonHeight})) {
        m_actions.newProject();
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open Project...", {fontSize * 11.0f, buttonHeight})) {
        m_actions.browseForProject();
    }
    ImGui::Dummy({0.0f, fontSize * 0.5f});

    Widgets::Heading("Recent Projects", 1.15f);
    if (!m_settings.GetRecentProjects().empty()) {
        ImGui::SameLine(width - fontSize * 14.0f);
        ImGui::SetNextItemWidth(fontSize * 14.0f);
        ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search", &m_search);
    }
    ImGui::Separator();
    DrawRecentProjects();

    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void ProjectHub::DrawRecentProjects() {
    const auto& recent = m_settings.GetRecentProjects();
    if (recent.empty()) {
        ImGui::Dummy({0.0f, ImGui::GetFontSize()});
        ImGui::TextColored(EditorColors::TextMuted,
                           "No recent projects. Create a new one, open an existing one, or drop a .spot file here.");
        return;
    }

    std::optional<std::filesystem::path> toRemove;
    const float rowHeight = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    const float width = ImGui::CalcItemWidth();
    for (const RecentProject& project : recent) {
        const std::string path = PathToUtf8(project.file);
        if (!ContainsIgnoringCase(project.name, m_search) && !ContainsIgnoringCase(path, m_search)) {
            continue;
        }
        std::error_code error;
        const bool missing = !std::filesystem::exists(project.file, error);

        ImGui::PushID(path.c_str());
        const ImVec2 start = ImGui::GetCursorPos();
        if (ImGui::Selectable("##Project", false, ImGuiSelectableFlags_AllowDoubleClick, {width, rowHeight}) &&
            !missing) {
            m_actions.openProject(project.file);
        }
        ImGui::SetItemTooltip("%s", missing ? "The project is no longer there." : path.c_str());
        if (ImGui::BeginPopupContextItem("##ProjectMenu")) {
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open", nullptr, false, !missing)) {
                m_actions.openProject(project.file);
            }
            if (ImGui::MenuItem(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in File Manager", nullptr, false, !missing)) {
                Shell::Open(project.file.parent_path());
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_XMARK "  Remove from List")) {
                toRemove = project.file;
            }
            ImGui::EndPopup();
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::SetCursorPos({start.x + style.FramePadding.x, start.y + style.ItemSpacing.y * 0.5f});
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, missing ? EditorColors::TextMuted : style.Colors[ImGuiCol_Text]);
        Widgets::Heading(project.name.c_str());
        ImGui::PopStyleColor();
        const std::string shown =
            missing ? "Missing" : Widgets::EllipsizeStart(path, width - style.FramePadding.x * 2.0f);
        ImGui::TextColored(EditorColors::TextMuted, "%s", shown.c_str());
        ImGui::EndGroup();

        const std::string when = Widgets::FormatRelativeTime(project.lastOpened);
        ImGui::SetCursorPos({start.x + width - ImGui::CalcTextSize(when.c_str()).x - style.FramePadding.x,
                             start.y + style.ItemSpacing.y * 0.5f});
        ImGui::TextColored(EditorColors::TextMuted, "%s", when.c_str());
        // The row's texts were placed over the selectable; this moves past the row the way ImGui expects.
        ImGui::SetCursorPos(start);
        ImGui::Dummy({width, rowHeight});
        ImGui::PopID();
    }

    if (toRemove) {
        m_settings.RemoveRecentProject(*toRemove);
        m_settings.Save();
    }
}

} // namespace Spot::Editor
