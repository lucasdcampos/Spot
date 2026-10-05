#include "project/editor_project.h"

#include "core/editor_log.h"

#include <utility>

namespace Spot::Editor {

std::unique_ptr<EditorProject> EditorProject::Open(const std::filesystem::path& file, GraphicsDevice* device) {
    auto project = Project::Load(file);
    if (!project) {
        return nullptr;
    }
    return std::unique_ptr<EditorProject>(new EditorProject(std::move(*project), device));
}

EditorProject::EditorProject(Project project, GraphicsDevice* device) : m_project(std::move(project)) {
    m_database = std::make_unique<AssetDatabase>(m_project.GetAssetsDirectory(), m_project.GetImportDirectory());
    const std::size_t imported = m_database->Refresh();
    m_assets = std::make_unique<AssetManager>(m_project.GetImportDirectory(), device);
    GetEditorLogger().Info("Opened project '{}' ({} assets, {} imported)", m_project.GetSettings().name,
                           m_database->GetAssets().size(), imported);
}

void EditorProject::Refresh() {
    // Deleted sources import nothing but still change the manifest, so it is always read again.
    m_database->Refresh();
    m_assets->RefreshManifest();
}

} // namespace Spot::Editor
