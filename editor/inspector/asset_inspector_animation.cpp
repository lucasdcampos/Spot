#include "inspector/asset_inspector.h"

#include "core/editor_log.h"
#include "project/editor_project.h"
#include "scene/scene_workspace.h"

#include <algorithm>
#include <fstream>

namespace Spot::Editor {

namespace {
bool WriteController(EditorProject& project, AssetId id, const std::string& text) {
    const auto* info = project.GetAssetDatabase().Find(id);
    if (!info || info->type != AssetType::AnimatorController || info->parent.IsValid() ||
        !AnimatorController::LoadFromString(text, &project.GetAssets())) {
        return false;
    }
    const auto path = project.GetProject().GetAssetsDirectory() / Utf8ToPath(info->path);
    {
        std::ofstream file(path, std::ios::binary);
        file << text;
        if (!file) {
            GetEditorLogger().Error("Could not save animator controller '{}'", PathToUtf8(path));
            return false;
        }
    }
    return project.Reimport(id);
}
class EditControllerCommand final : public Command {
public:
    EditControllerCommand(EditorProject& project, AssetId id, std::string before, std::string after)
        : m_project(project), m_id(id), m_before(std::move(before)), m_after(std::move(after)) {}
    std::string GetName() const override { return "Edit Animator Controller"; }
    bool Execute() override { return WriteController(m_project, m_id, m_after); }
    void Undo() override { WriteController(m_project, m_id, m_before); }

private:
    EditorProject& m_project;
    AssetId m_id;
    std::string m_before, m_after;
};
Animator* SelectedAnimator(SceneDocument* document) {
    if (!document) {
        return nullptr;
    }
    auto* entity = document->GetScene().GetEntity(document->GetSelection().GetPrimary());
    if (!entity) {
        return nullptr;
    }
    if (auto* renderer = entity->GetComponent<SkinnedMeshRenderer>()) {
        if (auto* owner = document->GetScene().GetEntity(renderer->GetAnimator())) {
            return owner->GetComponent<Animator>();
        }
    }
    return entity->GetComponentInParent<Animator>();
}
} // namespace

void AssetInspector::DrawController(EditorProject& project, const AssetInfo& info) {
    if (!m_controller) {
        ImGui::TextDisabled("The controller could not be loaded.");
        return;
    }
    int active = -1;
    if (auto* animator = SelectedAnimator(m_context.GetActiveDocument());
        animator && animator->GetController().GetId() == info.id) {
        active = animator->GetControllerRuntime().GetState();
    }
    m_graph.Draw(*m_controller, project, active);
    for (const auto& [key, rect] : m_graph.GetRects()) {
        m_rects[key] = rect;
    }
    const bool changed = m_controller->SaveToString() != m_controllerText;
    const bool valid = m_controller->IsValid();
    if (!valid) {
        ImGui::TextWrapped("Choose valid clips, unique names and existing parameters before applying.");
    }
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Revert controller")) {
        m_controller = AnimatorController::LoadFromString(m_controllerText, &project.GetAssets());
    }
    Record("Controller/Revert");
    ImGui::SameLine();
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Apply controller")) {
        const auto after = m_controller->SaveToString();
        if (m_history.Execute(std::make_unique<EditControllerCommand>(project, info.id, m_controllerText, after))) {
            m_controllerText = after;
            m_version = project.GetVersion();
        }
    }
    Record("Controller/Apply");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}

void AssetInspector::DrawClip(EditorProject& project, const AssetInfo& info) {
    const auto clip = project.GetAssets().Load<AnimationClip>(info.id);
    if (!clip) {
        return;
    }
    ImGui::Text("%.3f seconds, %zu channels, %zu events", clip->duration, clip->channels.size(), clip->events.size());
    auto* document = m_context.GetActiveDocument();
    auto* animator = SelectedAnimator(document);
    const bool playing = m_context.GetWorkspace() && m_context.GetWorkspace()->GetPlaySession();
    if (!animator || playing) {
        ImGui::TextWrapped("Select a rig in an edit-mode scene to preview this clip.");
    }
    float time = 0;
    if (animator && !playing) {
        auto& preview = document->GetAnimationPreview();
        preview.Select(document->GetScene(), animator->GetEntity().GetId(), animator->GetId(),
                       document->GetSceneRevision());
        preview.SetClip(clip);
        if (ImGui::Button(preview.IsPlaying() ? "Pause clip" : "Play clip")) {
            preview.SetPlaying(!preview.IsPlaying());
        }
        Record("Clip/Play");
        ImGui::SameLine();
        if (ImGui::Button("Restart clip")) {
            preview.Restart(document->GetScene());
        }
        time = preview.GetTime(document->GetScene());
        if (ImGui::SliderFloat("Clip time", &time, 0, clip->duration, "%.3f s")) {
            preview.SetPlaying(false);
            preview.Seek(document->GetScene(), time);
        }
        Record("Clip/Time");
    }
    const auto min = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, {min.x + width, min.y + 48}, ImGui::GetColorU32(ImGuiCol_FrameBg), 3);
    const auto x = [&](float sample) { return min.x + (clip->duration > 0 ? sample / clip->duration : 0) * width; };
    for (const auto& channel : clip->channels) {
        for (const float key : channel.times) {
            draw->AddLine({x(key), min.y + 8}, {x(key), min.y + 26}, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        }
    }
    for (const auto& event : clip->events) {
        draw->AddCircleFilled({x(event.time), min.y + 36}, 4, ImGui::GetColorU32(ImGuiCol_CheckMark));
    }
    draw->AddLine({x(time), min.y}, {x(time), min.y + 48}, ImGui::GetColorU32(ImGuiCol_PlotLines), 2);
    ImGui::Dummy({width, 48});
    Record("Clip/Timeline");
    for (const auto& event : clip->events) {
        ImGui::Text("%.3f  %s  %s", event.time, event.name.c_str(), event.payload.c_str());
    }
}

} // namespace Spot::Editor
