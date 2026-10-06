#pragma once

#include "assets/asset_id.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

class Archive;

// What a project file holds: everything about a game that is not an asset.
struct ProjectSettings {
    std::string name = {};
    // The game's own version, such as "1.2.0".
    std::string version = "0.1.0";
    // The scene a game made of assets starts with.
    AssetId startupScene = {};
    // Where the project's source assets are, relative to the project's folder.
    std::string assetsDirectory = "assets";

    void Serialize(Archive& archive);
};

// A game made with assets: a "<name>.spot" file in the project's folder, beside its source assets and a ".spot"
// folder for what is generated from them, such as imported assets, which is not meant for version control.
class Project {
public:
    static constexpr std::string_view FileExtension = ".spot";

    // Returns null, after logging why, when the file cannot be read or is not a project.
    static std::optional<Project> Load(const std::filesystem::path& file);
    // Makes a project file named after the project in the folder, which is created if needed, with an empty assets
    // folder. Returns null, after logging why, when they cannot be written.
    static std::optional<Project> Create(const std::filesystem::path& directory, ProjectSettings settings);

    static std::optional<ProjectSettings> LoadSettingsFromString(std::string_view text);
    static std::string SaveSettingsToString(const ProjectSettings& settings);

    bool Save() const;

    const std::filesystem::path& GetFile() const { return m_file; }
    std::filesystem::path GetDirectory() const { return m_file.parent_path(); }
    std::filesystem::path GetAssetsDirectory() const;
    // Generated files: imported assets, caches and editor state.
    std::filesystem::path GetCacheDirectory() const;
    std::filesystem::path GetImportDirectory() const;

    ProjectSettings& GetSettings() { return m_settings; }
    const ProjectSettings& GetSettings() const { return m_settings; }

private:
    Project(std::filesystem::path file, ProjectSettings settings);

    std::filesystem::path m_file;
    ProjectSettings m_settings;
};

} // namespace Spot
