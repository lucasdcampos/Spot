#include "project/editor_project.h"
#include <spot/animation/animation_assets.h>
#include <spot/assets/script_assets.h>
#include <spot/audio/audio_assets.h>
#include <spot/graphics/graphics_assets.h>
#include <spot/render/render_assets.h>
#include <spot/scene/scene_assets.h>

#include <spot_tools.h>

#include "core/editor_log.h"

#include <algorithm>
#include <utility>

namespace Spot::Editor {

std::unique_ptr<EditorProject> EditorProject::Open(const std::filesystem::path& file, GraphicsDevice* device) {
    auto project = Project::Load(file);
    if (!project) {
        return nullptr;
    }
    UpdateProjectLayout(*project);
    return std::unique_ptr<EditorProject>(new EditorProject(std::move(*project), device));
}

EditorProject::EditorProject(Project project, GraphicsDevice* device) : m_project(std::move(project)) {
    m_database = std::make_unique<AssetDatabase>(m_project.GetAssetsDirectory(), m_project.GetImportDirectory());
    const std::size_t imported = m_database->Refresh();
    m_assets = std::make_unique<AssetManager>(m_project.GetImportDirectory());
    RegisterGraphicsAssets(*m_assets, device);
    RegisterRenderAssets(*m_assets, device);
    RegisterAudioAssets(*m_assets);
    RegisterAnimationAssets(*m_assets);
    RegisterScriptAssets(*m_assets);
    RegisterSceneAssets(*m_assets);
    m_modules = std::make_unique<ModuleHost>(m_project, ModuleHost::Options{});
    // Scripts written in other editors are checked against the API of the engine the project is opened with.
    WriteLuauTypes(m_project);
    GetEditorLogger().Info("Opened project '{}' ({} assets, {} imported)", m_project.GetSettings().name,
                           m_database->GetAssets().size(), imported);
}

void EditorProject::Refresh() {
    // Deleted sources import nothing but still change the manifest, so it is always read again.
    m_database->Refresh();
    AfterImport();
}

bool EditorProject::Reimport(AssetId id) {
    const bool imported = m_database->Reimport(id);
    AfterImport();
    return imported;
}

void EditorProject::AfterImport() {
    m_assets->RefreshManifest();
    // What changed loads anew from now on; references to the old versions are for their holders to reload.
    for (const AssetId id : DevToolsModule::ReloadAssets(*m_assets, m_database->GetChanged())) {
        if (std::ranges::find(m_reloaded, id) == m_reloaded.end()) {
            m_reloaded.push_back(id);
        }
    }
    ++m_version;
}

} // namespace Spot::Editor
