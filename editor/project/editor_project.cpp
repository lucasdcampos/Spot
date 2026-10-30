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
#include <string_view>
#include <utility>

namespace Spot::Editor {

EditorProject::Opening::Opening(Project project)
    : m_project(std::move(project)),
      m_database(std::make_unique<AssetDatabase>(m_project.GetAssetsDirectory(), m_project.GetImportDirectory())),
      m_thread([this] {
          m_imported = m_database->Refresh([this](std::string_view path, std::size_t index, std::size_t count) {
              const std::scoped_lock lock(m_mutex);
              m_progress = {std::string(path), index, count};
          });
          m_done = true;
      }) {
}

EditorProject::Opening::~Opening() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

EditorProject::Opening::Progress EditorProject::Opening::GetProgress() const {
    const std::scoped_lock lock(m_mutex);
    return m_progress;
}

std::chrono::duration<float> EditorProject::Opening::GetDuration() const {
    return std::chrono::steady_clock::now() - m_started;
}

std::unique_ptr<EditorProject> EditorProject::Opening::Finish(GraphicsDevice* device) {
    if (m_thread.joinable()) {
        m_thread.join();
    }
    return std::unique_ptr<EditorProject>(
        new EditorProject(std::move(m_project), std::move(m_database), m_imported, device));
}

std::unique_ptr<EditorProject::Opening> EditorProject::BeginOpen(const std::filesystem::path& file) {
    auto project = Project::Load(file);
    if (!project) {
        return nullptr;
    }
    UpdateProjectLayout(*project);
    return std::unique_ptr<Opening>(new Opening(std::move(*project)));
}

std::unique_ptr<EditorProject> EditorProject::Open(const std::filesystem::path& file, GraphicsDevice* device) {
    const auto opening = BeginOpen(file);
    return opening ? opening->Finish(device) : nullptr;
}

EditorProject::EditorProject(Project project, std::unique_ptr<AssetDatabase> database, std::size_t imported,
                             GraphicsDevice* device)
    : m_project(std::move(project)), m_database(std::move(database)) {
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

std::optional<AssetId> EditorProject::ExtractMaterial(AssetId material, std::string_view path) {
    auto id = m_database->ExtractMaterial(material, path);
    AfterImport();
    return id;
}

std::vector<AssetId> EditorProject::ExtractMaterials(AssetId model, std::string_view folder) {
    auto ids = m_database->ExtractMaterials(model, folder);
    AfterImport();
    return ids;
}

std::vector<std::string> EditorProject::ExtractTextures(AssetId model, std::string_view folder) {
    auto paths = m_database->ExtractTextures(model, folder);
    AfterImport();
    return paths;
}

std::size_t EditorProject::RemapMaterialsByName(AssetId model) {
    const std::size_t count = m_database->RemapMaterialsByName(model);
    AfterImport();
    return count;
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
