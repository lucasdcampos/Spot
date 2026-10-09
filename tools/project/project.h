#pragma once

#include <spot.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

// What a project file holds: everything about a game that is not an asset.
struct ProjectSettings {
    std::string name = {};
    // The game's own version, such as "1.2.0".
    std::string version = "0.1.0";
    // The scene the game starts with.
    AssetId startupScene = {};
};

// A game being made: its assets, its code and its settings, which the tools build into the game. Games never see
// their project, only the GameSettings their build derives from it. Every project's folder has the same layout:
//   <name>.spot   the project file, with its settings
//   assets/       the source assets, each beside its .meta file
//   src/          the game's C++ code with its CMakeLists.txt and main.cpp
//   builds/       games exported from the editor
//   .spot/        what is generated from the rest, such as imported assets, and the editor's state
// The last two are not meant for version control.
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
    // The game's code.
    std::filesystem::path GetCodeDirectory() const;
    // Generated files: imported assets, caches and editor state.
    std::filesystem::path GetCacheDirectory() const;
    std::filesystem::path GetImportDirectory() const;
    // Where the tools build the game's code (see spot_add_game() in the engine's CMake package).
    std::filesystem::path GetBuildDirectory() const;
    // The game module built from the project's code, which the editor loads.
    std::filesystem::path GetModuleFile() const;
    // The game's executable built from the project's code, which runs it from the GameSettings beside it.
    std::filesystem::path GetGameExecutable() const;

    ProjectSettings& GetSettings() { return m_settings; }
    const ProjectSettings& GetSettings() const { return m_settings; }
    // What the game is told about itself when it runs.
    GameSettings GetGameSettings() const;

private:
    Project(std::filesystem::path file, ProjectSettings settings);

    std::filesystem::path m_file;
    ProjectSettings m_settings;
};

} // namespace Spot
