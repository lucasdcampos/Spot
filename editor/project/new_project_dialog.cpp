#include "project/new_project_dialog.h"

#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cfloat>
#include <system_error>

namespace Spot::Editor {

namespace {

constexpr const char* PopupName = "New Project";

// A name not taken in the folder yet: "New Project", then "New Project 2" and on.
std::string MakeUniqueName(const std::filesystem::path& location) {
    std::string name = "New Project";
    std::error_code error;
    for (int number = 2; std::filesystem::exists(location / Utf8ToPath(name), error); ++number) {
        name = "New Project " + std::to_string(number);
    }
    return name;
}

} // namespace

NewProjectDialog::NewProjectDialog(EditorSettings& settings, Window& window) : m_settings(settings), m_window(window) {
}

void NewProjectDialog::Open() {
    m_openRequested = true;
    m_location = PathToUtf8(m_settings.GetProjectsLocation());
    m_name = MakeUniqueName(m_settings.GetProjectsLocation());
    m_error.clear();
}

std::optional<std::filesystem::path> NewProjectDialog::Draw() {
    if (m_openRequested) {
        ImGui::OpenPopup(PopupName);
        m_openRequested = false;
    }
    if (m_chosenLocation->path) {
        m_location = PathToUtf8(*m_chosenLocation->path);
        m_chosenLocation->path.reset();
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({ImGui::GetFontSize() * 36.0f, 0.0f}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(PopupName, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> created;
    if (Widgets::BeginProperties("##NewProject")) {
        Widgets::PropertyLabel("Name");
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputText("##Name", &m_name);

        Widgets::PropertyLabel("Location");
        const float browseWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##Location", &m_location);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            FileDialog::OpenFolder(
                &m_window,
                [chosen = m_chosenLocation](std::vector<std::filesystem::path> paths) {
                    if (!paths.empty()) {
                        chosen->path = paths.front();
                    }
                },
                Utf8ToPath(m_location));
        }

        Widgets::PropertyLabel("Template");
        for (const ProjectTemplateInfo& info : GetProjectTemplates()) {
            ImGui::PushID(info.name);
            const bool selected = m_template == info.kind;
            if (ImGui::Selectable("##Template", selected, ImGuiSelectableFlags_None,
                                  {0.0f, ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y})) {
                m_template = info.kind;
            }
            ImGui::SameLine(ImGui::GetStyle().ItemSpacing.x);
            ImGui::BeginGroup();
            Widgets::Heading(info.name);
            ImGui::TextColored(EditorColors::TextMuted, "%s", info.description);
            ImGui::EndGroup();
            ImGui::PopID();
        }
        Widgets::EndProperties();
    }

    const std::filesystem::path location = Utf8ToPath(m_location);
    const std::string nameProblem = ValidateProjectName(m_name);
    std::error_code error;
    const bool exists = nameProblem.empty() && std::filesystem::exists(location / Utf8ToPath(m_name), error) &&
                        !std::filesystem::is_empty(location / Utf8ToPath(m_name), error);
    const std::string problem = !nameProblem.empty() ? nameProblem
                                : m_location.empty() ? "Choose where to create the project."
                                : exists             ? "A folder with this name already exists there."
                                                     : m_error;

    ImGui::Spacing();
    if (!problem.empty()) {
        ImGui::TextColored(EditorColors::Error, ICON_FA_CIRCLE_EXCLAMATION "  %s", problem.c_str());
    } else {
        ImGui::TextColored(EditorColors::TextMuted, "Creates %s",
                           PathToUtf8(location / Utf8ToPath(m_name) / Utf8ToPath(m_name + ".spot")).c_str());
    }
    ImGui::Separator();

    const float buttonWidth = ImGui::GetFontSize() * 6.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x -
                         ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("Cancel", {buttonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!nameProblem.empty() || m_location.empty() || exists);
    ImGui::PushStyleColor(ImGuiCol_Button, EditorColors::Accent);
    // Enter confirms from anywhere in the dialog, including while typing the name.
    const bool create = ImGui::Button("Create", {buttonWidth, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (create && nameProblem.empty() && !m_location.empty() && !exists) {
        created = CreateProject(location, m_name, m_template);
        if (created) {
            m_settings.SetProjectsLocation(location);
            ImGui::CloseCurrentPopup();
        } else {
            m_error = "The project could not be created; see the console for why.";
        }
    }
    ImGui::EndPopup();
    return created;
}

} // namespace Spot::Editor
