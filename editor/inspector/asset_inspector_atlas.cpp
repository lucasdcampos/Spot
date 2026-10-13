#include "imgui/widgets.h"
#include "inspector/asset_inspector.h"
#include "inspector/inspector_backend.h"
#include "project/editor_project.h"
#include <fstream>

namespace Spot::Editor {
namespace {
bool WriteAtlas(EditorProject& project, AssetId id, const std::string& text) {
    const auto* info = project.GetAssetDatabase().Find(id);
    if (!info || info->type != AssetType::SpriteAtlas || !SpriteAtlas::LoadFromString(text, &project.GetAssets()))
        return false;
    std::ofstream file(project.GetProject().GetAssetsDirectory() / Utf8ToPath(info->path), std::ios::binary);
    file << text;
    file.close();
    if (!file)
        return false;
    return project.Reimport(id);
}
class EditAtlasCommand final : public Command {
public:
    EditAtlasCommand(EditorProject& project, AssetId id, std::string before, std::string after)
        : m_project(project), m_id(id), m_before(std::move(before)), m_after(std::move(after)) {}
    std::string GetName() const override { return "Edit Sprite Atlas"; }
    bool Execute() override { return m_before != m_after && WriteAtlas(m_project, m_id, m_after); }
    void Undo() override { WriteAtlas(m_project, m_id, m_before); }

private:
    EditorProject& m_project;
    AssetId m_id;
    std::string m_before, m_after;
};
} // namespace
void AssetInspector::DrawAtlas(EditorProject& project, const AssetInfo& info) {
    if (!m_atlas) {
        ImGui::TextDisabled("The atlas could not be loaded.");
        return;
    }
    if (Widgets::BeginProperties("AtlasFields")) {
        InspectorBackend backend({.project = &project, .root = ""});
        Archive archive(backend);
        m_atlas->Serialize(archive);
        for (const auto& field : backend.GetFieldRects())
            m_rects[field.pointer] = {field.min, field.max};
        Widgets::EndProperties();
    }
    const bool changed = m_atlas->SaveToString() != m_atlasText;
    const bool valid = m_atlas->IsValid();
    if (!valid)
        ImGui::TextWrapped("Choose a texture, valid pixel rectangles, unique names and positive frame durations.");
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Revert atlas"))
        m_atlas = SpriteAtlas::LoadFromString(m_atlasText, &project.GetAssets());
    Record("Atlas/Revert");
    ImGui::SameLine();
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Apply atlas")) {
        const auto after = m_atlas->SaveToString();
        if (m_history.Execute(std::make_unique<EditAtlasCommand>(project, info.id, m_atlasText, after))) {
            m_atlasText = after;
            m_version = project.GetVersion();
        }
    }
    Record("Atlas/Apply");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}
} // namespace Spot::Editor
