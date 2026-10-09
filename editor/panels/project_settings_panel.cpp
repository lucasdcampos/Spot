#include "panels/project_settings_panel.h"

#include "core/text.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"
#include "inspector/inspector_backend.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <format>
#include <string>
#include <vector>

namespace Spot::Editor {

ProjectSettingsPanel::ProjectSettingsPanel(EditorContext& context)
    : Panel("ProjectSettings", ICON_FA_GEAR "  Project Settings"), m_context(context) {
    SetOpen(false);
}

void ProjectSettingsPanel::SetUpWindow() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({ImGui::GetFontSize() * 34.0f, ImGui::GetFontSize() * 26.0f}, ImGuiCond_FirstUseEver);
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

    settings.sections.ForEach([this, project](std::string_view name, auto& section) {
        ImGui::Spacing();
        Widgets::Heading(MakeDisplayName(name).c_str());
        const std::string id = "##" + std::string(name);
        if (!Widgets::BeginProperties(id.c_str())) {
            return;
        }
        InspectorBackend backend({.project = project, .root = "/" + std::string(name)});
        Archive archive(backend);
        section.Serialize(archive);
        Widgets::EndProperties();
        m_sectionsChanged = m_sectionsChanged || backend.WasChanged();
    });
    DrawLayers(settings.sections.physics);
    // Once a drag or an edit ends, rather than on every change it makes.
    if (m_sectionsChanged && !ImGui::IsAnyItemActive()) {
        m_sectionsChanged = false;
        Save();
    }

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in File Manager")) {
        Shell::Open(project->GetProject().GetDirectory());
    }
}

void ProjectSettingsPanel::DrawLayers(PhysicsSettings& physics) {
    ImGui::Spacing();
    Widgets::Heading("Layers");
    if (ImGui::TreeNodeEx("Names", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (Widgets::BeginProperties("##layerNames")) {
            for (int layer = 0; layer < Entity::LayerCount; ++layer) {
                Widgets::PropertyLabel(std::format("Layer {}", layer).c_str(),
                                       layer == 0 ? "Entities start on the default layer, which keeps its name"
                                                  : nullptr);
                ImGui::PushID(layer);
                ImGui::BeginDisabled(layer == 0);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "Unused", &physics.layerNames[static_cast<std::size_t>(layer)]);
                m_sectionsChanged = m_sectionsChanged || ImGui::IsItemDeactivatedAfterEdit();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            Widgets::EndProperties();
        }
        ImGui::TreePop();
    }

    // Only named layers are in use, so only they show, each pair once.
    std::vector<int> named;
    for (int layer = 0; layer < Entity::LayerCount; ++layer) {
        if (!physics.layerNames[static_cast<std::size_t>(layer)].empty()) {
            named.push_back(layer);
        }
    }
    if (!ImGui::TreeNodeEx("Collisions", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::TextDisabled("Bodies of two layers collide when their box is checked.");
    const auto columns = static_cast<int>(named.size()) + 1;
    if (ImGui::BeginTable("##collisions", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoHeaderLabel);
        // Columns go from the last layer to the first, so the checked pairs form a triangle.
        for (auto layer = named.rbegin(); layer != named.rend(); ++layer) {
            ImGui::TableSetupColumn(physics.layerNames[static_cast<std::size_t>(*layer)].c_str(),
                                    ImGuiTableColumnFlags_AngledHeader);
        }
        ImGui::TableAngledHeadersRow();
        for (std::size_t row = 0; row < named.size(); ++row) {
            const int a = named[row];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(physics.layerNames[static_cast<std::size_t>(a)].c_str());
            for (std::size_t column = 0; column + row < named.size(); ++column) {
                const int b = named[named.size() - 1 - column];
                ImGui::TableNextColumn();
                bool collides = physics.Collides(a, b);
                ImGui::PushID(a * Entity::LayerCount + b);
                if (ImGui::Checkbox("##collides", &collides)) {
                    physics.SetCollides(a, b, collides);
                    m_sectionsChanged = true;
                }
                ImGui::SetItemTooltip("%s and %s", physics.layerNames[static_cast<std::size_t>(a)].c_str(),
                                      physics.layerNames[static_cast<std::size_t>(b)].c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::TreePop();
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
