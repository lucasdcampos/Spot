#include "app/preferences_window.h"

#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace Spot::Editor {

namespace {

constexpr std::array UiScales{0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f};

std::string FormatScale(float scale) {
    return std::format("{:.0f}%", scale * 100.0f);
}

void Section(const char* title) {
    ImGui::Spacing();
    Widgets::Heading(title);
    ImGui::Separator();
}

} // namespace

PreferencesWindow::PreferencesWindow(EditorSettings& settings, Window& window)
    : m_settings(settings), m_window(window) {
}

void PreferencesWindow::Draw() {
    if (m_chosenLocation->path) {
        m_settings.SetProjectsLocation(*m_chosenLocation->path);
        m_chosenLocation->path.reset();
        m_settings.Save();
    }
    if (!m_open) {
        return;
    }
    const float fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowSize({fontSize * 34.0f, fontSize * 36.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    const bool visible = ImGui::Begin(ICON_FA_SLIDERS "  Preferences###Preferences", &m_open,
                                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
    if (!visible) {
        ImGui::End();
        return;
    }

    EditorPreferences& preferences = m_settings.GetPreferences();
    GizmoSettings& gizmo = m_settings.GetGizmo();
    bool changed = false;

    Section("Interface");
    if (Widgets::BeginProperties("##Interface")) {
        Widgets::PropertyLabel("UI scale", "Grows or shrinks text and controls, beyond what the display's scale makes "
                                           "them");
        if (ImGui::BeginCombo("##UiScale", FormatScale(preferences.uiScale).c_str())) {
            for (const float scale : UiScales) {
                const bool selected = std::abs(preferences.uiScale - scale) < 0.001f;
                if (ImGui::Selectable(FormatScale(scale).c_str(), selected)) {
                    preferences.uiScale = scale;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        Widgets::EndProperties();
    }

    Section("Scene View");
    if (Widgets::BeginProperties("##SceneView")) {
        Widgets::PropertyLabel("Move snap", "How far moving snaps, in units");
        ImGui::DragFloat("##Move", &gizmo.moveStep, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        Widgets::PropertyLabel("Rotate snap", "How far turning snaps, in degrees");
        ImGui::DragFloat("##Rotate", &gizmo.rotateStep, 0.5f, 0.1f, 180.0f, "%.1f\xC2\xB0",
                         ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        Widgets::PropertyLabel("Scale snap", "How far scaling snaps");
        ImGui::DragFloat("##Scale", &gizmo.scaleStep, 0.005f, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        Widgets::EndProperties();
    }

    Section("Play");
    if (Widgets::BeginProperties("##Play")) {
        Widgets::PropertyLabel("Clear console", "Clears the console when a game starts playing, so it shows only "
                                                "what the game logs");
        changed |= ImGui::Checkbox("##ClearConsole", &preferences.clearConsoleOnPlay);
        Widgets::EndProperties();
    }

    Section("Game Code");
    if (Widgets::BeginProperties("##Code")) {
        Widgets::PropertyLabel("Build on change", "Builds the game's code as soon as it changes; otherwise only with "
                                                  "Ctrl+B");
        changed |= ImGui::Checkbox("##BuildOnChange", &preferences.buildOnChange);
        Widgets::EndProperties();
    }

    Section("Scripts");
    if (Widgets::BeginProperties("##Scripts")) {
        Widgets::PropertyLabel("External editor",
                               "The command that opens the project or a script in your code editor, with {project}, "
                               "{file} and {line} where they go, such as: code {project} --goto {file}:{line}. "
                               "Empty opens them with the applications the system opens them with");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##ExternalEditor", "code {project} --goto {file}:{line}",
                                 &preferences.externalEditor);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        Widgets::EndProperties();
    }

    Section("Scenes");
    if (Widgets::BeginProperties("##Scenes")) {
        Widgets::PropertyLabel("Back up every", "Keeps a copy of scenes with unsaved changes, which the editor offers "
                                                "back if it stops before they are saved");
        ImGui::DragInt("##Backup", &preferences.backupMinutes, 0.2f, 0, EditorPreferences::MaxBackupMinutes,
                       preferences.backupMinutes == 0 ? "Never" : "%d min", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        Widgets::EndProperties();
    }

    Section("Projects");
    if (Widgets::BeginProperties("##Projects")) {
        Widgets::PropertyLabel("Location", "Where new projects are created, unless another folder is chosen");
        const std::string location = PathToUtf8(m_settings.GetProjectsLocation());
        const float browseWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float textWidth = ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x;
        const float start = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Widgets::EllipsizeStart(location, textWidth).c_str());
        ImGui::SetItemTooltip("%s", location.c_str());
        ImGui::SameLine(start + textWidth + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("Browse...")) {
            FileDialog::OpenFolder(
                &m_window,
                [chosen = m_chosenLocation](std::vector<std::filesystem::path> paths) {
                    if (!paths.empty()) {
                        chosen->path = paths.front();
                    }
                },
                m_settings.GetProjectsLocation());
        }
        Widgets::EndProperties();
    }

    ImGui::End();
    if (changed) {
        m_settings.Save();
    }
}

} // namespace Spot::Editor
