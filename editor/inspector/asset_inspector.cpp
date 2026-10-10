#include "inspector/asset_inspector.h"

#include "assets/asset_list.h"
#include "assets/thumbnail_cache.h"
#include "core/editor_log.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/imgui_renderer.h"
#include "imgui/widgets.h"
#include "inspector/inspector_backend.h"
#include "project/editor_project.h"
#include "scene/scene_workspace.h"

#include <imgui_internal.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <utility>

namespace Spot::Editor {

namespace {

// Writes a material's text to its source file and imports it, so scenes and thumbnails show it.
bool WriteMaterial(EditorProject& project, AssetId material, const std::string& text) {
    const AssetInfo* info = project.GetAssetDatabase().Find(material);
    if (!info) {
        return false;
    }
    {
        std::ofstream file(project.GetProject().GetAssetsDirectory() / Utf8ToPath(info->path), std::ios::binary);
        file << text;
        if (!file) {
            GetEditorLogger().Error("Could not save the material '{}'", info->path);
            return false;
        }
    }
    // Imported at once, whether or not the file's time of change tells it apart from the last save.
    project.Reimport(material);
    return true;
}

class EditMaterialCommand final : public Command {
public:
    EditMaterialCommand(EditorProject& project, AssetId material, std::string before, std::string after)
        : m_project(project), m_material(material), m_before(std::move(before)), m_after(std::move(after)) {}

    std::string GetName() const override { return "Edit Material"; }
    bool Execute() override { return m_before != m_after && WriteMaterial(m_project, m_material, m_after); }
    void Undo() override { WriteMaterial(m_project, m_material, m_before); }

private:
    EditorProject& m_project;
    AssetId m_material;
    std::string m_before;
    std::string m_after;
};

bool HasImportSettings(AssetType type) {
    return type == AssetType::Texture || type == AssetType::Model || type == AssetType::Font ||
           type == AssetType::Sound;
}

} // namespace

AssetInspector::AssetInspector(EditorContext& context) : m_context(context) {
}

CommandHistory* AssetInspector::GetHistory() {
    return m_material ? &m_history : nullptr;
}

std::optional<std::pair<ImVec2, ImVec2>> AssetInspector::GetItemRect(std::string_view item) const {
    const auto found = m_rects.find(std::string(item));
    return found != m_rects.end() ? std::optional(found->second) : std::nullopt;
}

void AssetInspector::Record(const std::string& item) {
    m_rects[item] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
}

void AssetInspector::Load(EditorProject& project, AssetId asset) {
    const bool other = &project != m_project || asset != m_asset;
    m_project = &project;
    m_asset = asset;
    m_version = project.GetVersion();
    if (other) {
        m_history.Clear();
        m_edit.reset();
        m_settings.reset();
    }

    const AssetInfo* info = project.GetAssetDatabase().Find(asset);
    m_material.reset();
    m_materialText.clear();
    if (info && info->type == AssetType::Material && !info->parent.IsValid()) {
        m_material = MaterialSource::LoadFromFile(project.GetProject().GetAssetsDirectory() / Utf8ToPath(info->path));
        m_materialText = m_material ? m_material->SaveToString() : std::string();
    }
    // Settings being changed are kept until applied or reverted, unless they are another asset's.
    const AssetMeta* meta = info ? project.GetAssetDatabase().GetMeta(asset) : nullptr;
    const bool changed = m_settings && meta && m_settings->GetSettingsString() != meta->GetSettingsString();
    if (!meta) {
        m_settings.reset();
    } else if (!changed) {
        m_settings = *meta;
    }
}

void AssetInspector::FinishEdit() {
    if (!m_edit || !m_project || !m_material) {
        return;
    }
    const ImGuiID active = ImGui::GetActiveID();
    if (active != 0 && active == m_edit->widget) {
        return;
    }
    const Edit edit = std::exchange(m_edit, std::nullopt).value();
    std::string after = m_material->SaveToString();
    if (after == edit.before) {
        // Previews may have saved values the edit went through before coming back.
        WriteMaterial(*m_project, m_asset, edit.before);
        return;
    }
    m_materialText = after;
    m_history.Execute(std::make_unique<EditMaterialCommand>(*m_project, m_asset, edit.before, std::move(after)));
}

void AssetInspector::Draw(EditorProject& project, AssetId asset) {
    m_rects.clear();
    if (&project != m_project || asset != m_asset) {
        Load(project, asset);
    } else if (project.GetVersion() != m_version && !m_edit) {
        // Changed by undo, by an import, or in another application.
        Load(project, asset);
    }
    const AssetInfo* info = project.GetAssetDatabase().Find(asset);
    if (!info) {
        ImGui::TextDisabled("The asset is gone.");
        return;
    }

    DrawHeader(project, *info);
    ImGui::Separator();
    if (ImGui::BeginChild("##Body")) {
        if (info->parent.IsValid()) {
            DrawSubAsset(project, *info);
        }
        if (m_material || (info->type == AssetType::Material && !info->parent.IsValid())) {
            DrawMaterial(project, *info);
        }
        if (HasImportSettings(info->type) && !info->parent.IsValid()) {
            DrawImportSettings(project, *info);
        }
        DrawPreview(project, *info);
    }
    ImGui::EndChild();
}

void AssetInspector::DrawHeader(EditorProject& project, const AssetInfo& info) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float side = std::round(ImGui::GetFrameHeight() * 2.4f);
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(corner, {corner.x + side, corner.y + side}, ImGui::GetColorU32(ImGuiCol_FrameBg),
                            style.FrameRounding);
    ThumbnailCache* thumbnails = m_context.GetThumbnails();
    if (const Texture* picture = thumbnails ? thumbnails->Get(project, info.id, info.type) : nullptr) {
        drawList->AddImage(ToImTextureId(*picture), {corner.x + 2.0f, corner.y + 2.0f},
                           {corner.x + side - 2.0f, corner.y + side - 2.0f});
    } else {
        const char* icon = GetAssetTypeIcon(info.type);
        const float size = side * 0.5f;
        const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
        drawList->AddText(ImGui::GetFont(), size,
                          {corner.x + (side - extent.x) * 0.5f, corner.y + (side - extent.y) * 0.5f},
                          ImGui::GetColorU32(EditorColors::Accent), icon);
    }
    ImGui::Dummy({side, side});
    ImGui::SameLine();

    ImGui::BeginGroup();
    const std::string name = info.parent.IsValid() ? info.name : PathToUtf8(Utf8ToPath(info.path).filename());
    Widgets::Heading(name.c_str(), 1.15f);
    ImGui::TextDisabled("%s  %s", std::string(GetAssetTypeName(info.type)).c_str(), info.path.c_str());
    if (ImGui::SmallButton(ICON_FA_COPY " Copy Id")) {
        Clipboard::SetText(info.id.ToString());
    }
    ImGui::SetItemTooltip("%s", info.id.ToString().c_str());
    if (info.type == AssetType::Scene) {
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN " Open Scene")) {
            if (SceneWorkspace* workspace = m_context.GetWorkspace()) {
                workspace->OpenScene(info.id);
            }
        }
    }
    ImGui::EndGroup();
}

void AssetInspector::DrawSubAsset(EditorProject& project, const AssetInfo& info) {
    const AssetInfo* file = project.GetAssetDatabase().Find(info.parent);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(EditorColors::TextMuted,
                       "Part of '%s', imported from it: change it there, or through its import settings.",
                       file ? file->path.c_str() : "?");
    ImGui::PopTextWrapPos();
}

void AssetInspector::DrawMaterial(EditorProject& project, const AssetInfo& info) {
    if (!m_material) {
        ImGui::TextColored(EditorColors::Warning, ICON_FA_TRIANGLE_EXCLAMATION "  '%s' could not be read.",
                           info.path.c_str());
        return;
    }
    if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen) ||
        !Widgets::BeginProperties("##Material")) {
        return;
    }
    InspectorBackend backend({.project = &project, .eulers = &m_eulers, .root = ""});
    Archive archive(backend);
    m_material->Serialize(archive);
    Widgets::EndProperties();
    for (const InspectorBackend::FieldRect& rect : backend.GetFieldRects()) {
        m_rects[rect.pointer] = {rect.min, rect.max};
    }
    if (!backend.WasChanged()) {
        return;
    }
    if (!m_edit) {
        m_edit = Edit{m_materialText};
    }
    m_edit->widget = ImGui::GetActiveID();
    // Scenes show the change while it is made, a few times a second, as saving imports it.
    const double now = ImGui::GetTime();
    if (now - m_lastPreview > 0.12 || m_edit->widget == 0) {
        m_lastPreview = now;
        WriteMaterial(project, m_asset, m_material->SaveToString());
        m_version = project.GetVersion();
    }
}

void AssetInspector::DrawImportSettings(EditorProject& project, const AssetInfo& info) {
    AssetMeta* meta = project.GetAssetDatabase().GetMeta(info.id);
    if (!meta || !m_settings || !ImGui::CollapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (Widgets::BeginProperties("##Import")) {
        InspectorBackend backend({.project = &project, .eulers = &m_eulers, .root = ""});
        Archive archive(backend);
        switch (info.type) {
        case AssetType::Texture:
            m_settings->texture.Serialize(archive);
            break;
        case AssetType::Model:
            m_settings->model.Serialize(archive);
            break;
        case AssetType::Font:
            m_settings->font.Serialize(archive);
            break;
        case AssetType::Sound:
            m_settings->sound.Serialize(archive);
            break;
        default:
            break;
        }
        Widgets::EndProperties();
        for (const InspectorBackend::FieldRect& rect : backend.GetFieldRects()) {
            m_rects[rect.pointer] = {rect.min, rect.max};
        }
    }

    // Importing takes a while for large files, so changes wait until they are applied.
    const bool changed = m_settings->GetSettingsString() != meta->GetSettingsString();
    ImGui::Spacing();
    if (changed) {
        ImGui::TextColored(EditorColors::Warning, ICON_FA_CIRCLE_EXCLAMATION "  Not applied yet");
        ImGui::SameLine();
    }
    const float width = ImGui::GetFontSize() * 5.5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width * 2.0f -
                                                                     ImGui::GetStyle().ItemSpacing.x));
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Revert", {width, 0.0f})) {
        m_settings = *meta;
    }
    Record("Revert");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, EditorColors::Accent);
    if (ImGui::Button("Apply", {width, 0.0f})) {
        meta->texture = m_settings->texture;
        meta->model = m_settings->model;
        meta->font = m_settings->font;
        meta->sound = m_settings->sound;
        project.GetAssetDatabase().SaveMeta(info.id);
        project.Reimport(info.id);
    }
    ImGui::PopStyleColor();
    Record("Apply");
    ImGui::EndDisabled();
}

void AssetInspector::DrawPreview(EditorProject& project, const AssetInfo& info) {
    if (info.type != AssetType::Texture) {
        return;
    }
    ThumbnailCache* thumbnails = m_context.GetThumbnails();
    const Texture* texture = thumbnails ? thumbnails->Get(project, info.id, info.type) : nullptr;
    if (!texture || !ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const float width = static_cast<float>(texture->GetWidth());
    const float height = static_cast<float>(texture->GetHeight());
    const float available = ImGui::GetContentRegionAvail().x;
    // Small textures are shown larger, as large as fits.
    const float scale = std::min(available / width, ImGui::GetFontSize() * 16.0f / height);
    ImGui::Image(ToImTextureId(*texture), {width * scale, height * scale});
    ImGui::TextDisabled("%u x %u pixels", texture->GetWidth(), texture->GetHeight());
}

} // namespace Spot::Editor
