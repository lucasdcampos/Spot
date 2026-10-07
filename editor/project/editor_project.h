#pragma once

#include <spot.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace Spot::Editor {

// A project open in the editor, with its source assets imported and its assets ready to load.
class EditorProject {
public:
    // Loads the project and imports what changed in its assets. Returns null, after logging why, when the file is
    // not a project. Without a graphics device, textures, meshes and fonts cannot load.
    static std::unique_ptr<EditorProject> Open(const std::filesystem::path& file, GraphicsDevice* device);

    EditorProject(const EditorProject&) = delete;
    EditorProject& operator=(const EditorProject&) = delete;

    Project& GetProject() { return m_project; }
    const Project& GetProject() const { return m_project; }
    AssetDatabase& GetAssetDatabase() { return *m_database; }
    AssetManager& GetAssets() { return *m_assets; }

    // Imports what changed in the assets, and lets the asset manager see it.
    void Refresh();
    // Imports the asset's source again, such as after its import settings changed.
    bool Reimport(AssetId id);
    // Changes with every refresh, so windows can tell when the assets may have changed.
    std::uint64_t GetVersion() const { return m_version; }

private:
    EditorProject(Project project, GraphicsDevice* device);

    Project m_project;
    std::unique_ptr<AssetDatabase> m_database;
    std::unique_ptr<AssetManager> m_assets;
    std::uint64_t m_version = 0;
};

} // namespace Spot::Editor
