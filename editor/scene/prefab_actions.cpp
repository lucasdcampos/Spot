#include "scene/prefab_actions.h"

#include "assets/asset_browser_model.h"
#include "core/editor_log.h"
#include "project/editor_project.h"

#include <fstream>
#include <iterator>

namespace Spot::Editor {

namespace {

std::string Read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool Write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) {
        GetEditorLogger().Error("Could not write prefab '{}'", PathToUtf8(file));
        return false;
    }
    return true;
}

} // namespace

std::optional<AssetId> CreatePrefab(EditorProject& project, const Entity& entity, std::string_view folder) {
    const auto prefab = Prefab::FromString(entity.SaveToString());
    if (!prefab) {
        return std::nullopt;
    }
    AssetDatabase& database = project.GetAssetDatabase();
    if (!folder.empty() && !database.CreateFolder(folder)) {
        return std::nullopt;
    }
    std::string name = entity.GetName();
    if (ValidateFileName(name)) {
        name = "Prefab";
    }
    const auto path = database.GetFreePath(JoinPath(std::string(folder), name + ".prefab"));
    if (!Write(database.GetSourceDirectory() / Utf8ToPath(path), prefab->GetEntityText())) {
        return std::nullopt;
    }
    project.Refresh();
    return database.FindByPath(path);
}

PrefabCommand::PrefabCommand(SceneDocument& document, EditorProject& project, EntityId rootId, PrefabAction action,
                             PrefabOverrideScope scope)
    : SceneCommand(document), m_project(project), m_action(action) {
    const Entity* root = GetScene().GetEntity(rootId);
    if (!root || !root->GetPrefabAssetId().IsValid()) {
        return;
    }
    document.SetAssetManager(&project.GetAssets());
    m_asset = root->GetPrefabAssetId();
    m_before = GetScene().SaveToString();
    if (action == PrefabAction::Unpack) {
        m_after = UnpackPrefabToString(*root).value_or(std::string());
    } else if (const auto edit = PreparePrefabEdit(*root, scope, action == PrefabAction::Apply, &project.GetAssets())) {
        m_after = edit->sceneText;
        m_sourceAfter = edit->prefabText;
        if (action == PrefabAction::Apply) {
            const AssetInfo* info = project.GetAssetDatabase().Find(m_asset);
            if (!info) {
                m_after.clear();
                return;
            }
            m_sourceBefore = Read(project.GetAssetDatabase().GetSourceDirectory() / Utf8ToPath(info->path));
            if (m_sourceBefore.empty()) {
                m_after.clear();
            }
        }
    }
}

std::string PrefabCommand::GetName() const {
    switch (m_action) {
    case PrefabAction::Apply:
        return "Apply Prefab Overrides";
    case PrefabAction::Revert:
        return "Revert Prefab Overrides";
    case PrefabAction::Unpack:
        return "Unpack Prefab";
    }
    return {};
}

bool PrefabCommand::WriteSource(std::string_view text) {
    if (text.empty()) {
        return true;
    }
    const AssetInfo* info = m_project.GetAssetDatabase().Find(m_asset);
    return info && Write(m_project.GetAssetDatabase().GetSourceDirectory() / Utf8ToPath(info->path), text) &&
           m_project.Reimport(m_asset);
}

bool PrefabCommand::Restore(std::string_view sceneText, std::string_view sourceText) {
    if (!WriteSource(sourceText)) {
        return false;
    }
    return GetDocument().ReplaceSceneText(sceneText);
}

bool PrefabCommand::Execute() {
    if (m_after.empty()) {
        return false;
    }
    if (!Restore(m_after, m_sourceAfter)) {
        WriteSource(m_sourceBefore);
        return false;
    }
    return true;
}

void PrefabCommand::Undo() {
    if (!Restore(m_before, m_sourceBefore)) {
        GetEditorLogger().Error("Could not undo '{}'", GetName());
        WriteSource(m_sourceAfter);
    }
}

} // namespace Spot::Editor
