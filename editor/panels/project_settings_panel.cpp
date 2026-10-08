#include "panels/project_settings_panel.h"

#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

namespace Spot::Editor {

ProjectSettingsPanel::ProjectSettingsPanel(EditorContext& context)
    : Panel("ProjectSettings", ICON_FA_GEAR "  Project Settings"), m_context(context) {
    SetOpen(false);
}

void ProjectSettingsPanel::SetUpWindow() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({ImGui::GetFontSize() * 34.0f, ImGui::GetFontSize() * 18.0f}, ImGuiCond_FirstUseEver);
}

void ProjectSettingsPanel::OnDraw() {
    EditorProject* project = m_context.GetProject();
    if (!project) {
        DrawEmptyState(ICON_FA_GEAR, "Open a project to see its settings.");
        return;
    }
    ProjectSettings& settings = project->GetProject().GetSettings();

    if (!Widgets::BeginProperties("##ProjectSettings")) {
        return;
    }
    Widgets::PropertyLabel("Name", "The game's name, as the editor and the hub show it.");
    ImGui::InputText("##Name", &settings.name);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Save();
    }

    Widgets::PropertyLabel("Version", "The game's own version, such as 1.0.0.");
    ImGui::InputText("##Version", &settings.version);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Save();
    }

    Widgets::PropertyLabel("Startup Scene", "The scene the game starts with.");
    const AssetDatabase& database = project->GetAssetDatabase();
    const AssetInfo* startup = settings.startupScene.IsValid() ? database.Find(settings.startupScene) : nullptr;
    const std::string preview = startup ? startup->path : settings.startupScene.IsValid() ? "Missing scene" : "None";
    if (ImGui::BeginCombo("##StartupScene", preview.c_str())) {
        if (ImGui::Selectable("None", !settings.startupScene.IsValid())) {
            settings.startupScene = {};
            Save();
        }
        for (const AssetInfo& asset : database.GetAssets()) {
            if (asset.type != AssetType::Scene || asset.parent.IsValid()) {
                continue;
            }
            if (ImGui::Selectable((ICON_FA_CUBE "  " + asset.path).c_str(), asset.id == settings.startupScene)) {
                settings.startupScene = asset.id;
                Save();
            }
        }
        ImGui::EndCombo();
    }

    Widgets::PropertyLabel("Project File");
    const std::string file = PathToUtf8(project->GetProject().GetFile());
    ImGui::TextColored(EditorColors::TextMuted, "%s", file.c_str());
    ImGui::SetItemTooltip("%s", file.c_str());
    Widgets::EndProperties();

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in File Manager")) {
        Shell::Open(project->GetProject().GetDirectory());
    }
}

void ProjectSettingsPanel::Save() {
    EditorProject* project = m_context.GetProject();
    if (project && project->GetProject().Save()) {
        const ProjectSettings& settings = project->GetProject().GetSettings();
        m_context.GetSettings().AddRecentProject(project->GetProject().GetFile(), settings.name);
        m_context.GetSettings().Save();
        m_context.GetWindow().SetTitle(settings.name + " - Spot Editor");
    }
}

} // namespace Spot::Editor
