#pragma once

#include "modules/module_host.h"

#include <spot.h>
#include <spot_tools.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Spot::Editor {

// A project open in the editor, with its source assets imported and its assets ready to load.
class EditorProject {
public:
    // A project being opened: what changed in its assets imports on a thread of its own, which can take long after
    // many sources changed, so the editor keeps drawing and shows how far it got.
    class Opening {
    public:
        struct Progress {
            // The source file being imported, empty until the first one.
            std::string path;
            std::size_t index = 0;
            // How many sources there are, imported or found unchanged.
            std::size_t count = 0;
        };

        // Waits for the import to end.
        ~Opening();

        Opening(const Opening&) = delete;
        Opening& operator=(const Opening&) = delete;

        const Project& GetProject() const { return m_project; }
        bool IsDone() const { return m_done; }
        Progress GetProgress() const;
        std::chrono::duration<float> GetDuration() const;
        // The project, ready to edit, once its assets are imported, which it waits for. Call it once. Without a
        // graphics device, textures, meshes and fonts cannot load.
        std::unique_ptr<EditorProject> Finish(GraphicsDevice* device);

    private:
        friend class EditorProject;
        explicit Opening(Project project);

        Project m_project;
        std::unique_ptr<AssetDatabase> m_database;
        std::size_t m_imported = 0;
        std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
        std::atomic<bool> m_done = false;
        mutable std::mutex m_mutex;
        Progress m_progress;
        // Last, so it starts once the rest is there.
        std::thread m_thread;
    };

    // Loads the project and starts importing what changed in its assets. Returns null, after logging why, when the
    // file is not a project.
    static std::unique_ptr<Opening> BeginOpen(const std::filesystem::path& file);
    // As BeginOpen(), then Finish() once the assets are imported.
    static std::unique_ptr<EditorProject> Open(const std::filesystem::path& file, GraphicsDevice* device);

    EditorProject(const EditorProject&) = delete;
    EditorProject& operator=(const EditorProject&) = delete;

    Project& GetProject() { return m_project; }
    const Project& GetProject() const { return m_project; }
    AssetDatabase& GetAssetDatabase() { return *m_database; }
    AssetManager& GetAssets() { return *m_assets; }
    // The game's code, for projects that have some.
    ModuleHost& GetModules() { return *m_modules; }

    // Imports what changed in the assets, and lets the asset manager see it.
    void Refresh();
    // Imports the asset's source again, such as after its import settings changed.
    bool Reimport(AssetId id);
    // AssetDatabase::ExtractMaterial(), ExtractMaterials(), ExtractTextures() and RemapMaterialsByName(), letting the
    // asset manager see what they import.
    std::optional<AssetId> ExtractMaterial(AssetId material, std::string_view path);
    std::vector<AssetId> ExtractMaterials(AssetId model, std::string_view folder);
    std::vector<std::string> ExtractTextures(AssetId model, std::string_view folder);
    std::size_t RemapMaterialsByName(AssetId model);
    // Changes with every refresh, so windows can tell when the assets may have changed.
    std::uint64_t GetVersion() const { return m_version; }
    // The assets whose new versions load since the last call, for open scenes to load their references to them
    // again.
    std::vector<AssetId> TakeReloadedAssets() { return std::exchange(m_reloaded, {}); }

private:
    EditorProject(Project project, std::unique_ptr<AssetDatabase> database, std::size_t imported,
                  GraphicsDevice* device);

    Project m_project;
    std::unique_ptr<AssetDatabase> m_database;
    std::unique_ptr<AssetManager> m_assets;
    std::uint64_t m_version = 0;
    std::vector<AssetId> m_reloaded;
    std::unique_ptr<ModuleHost> m_modules;

    void AfterImport();
};

} // namespace Spot::Editor
