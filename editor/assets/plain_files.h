#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Editor {

// A folder of files that are not assets, such as a project's code, with the file operations of AssetDatabase, so the
// asset browser handles both alike. Paths are relative to the folder, with '/' separators. Failures are logged.
class PlainFiles {
public:
    explicit PlainFiles(std::filesystem::path directory) : m_directory(std::move(directory)) {}

    // Creates a folder, with the folders above it.
    bool CreateFolder(std::string_view path);
    // Moves or renames a file, or a folder with everything in it, to a path nothing uses yet.
    bool Move(std::string_view from, std::string_view to);
    // Deletes a file, or a folder with everything in it.
    bool Delete(std::string_view path);
    // Copies a file to a path nothing uses yet.
    bool Duplicate(std::string_view from, std::string_view to);
    // Copies files and folders from elsewhere into a folder, under names nothing uses yet. Returns the paths they got.
    std::vector<std::string> CopyIn(std::span<const std::filesystem::path> files, std::string_view folder);
    // The path when nothing uses it, or else the first free one with a number after the name: "player 2.cpp" for
    // "player.cpp".
    std::string GetFreePath(std::string_view path) const;

private:
    // The path in the folder; nothing, after logging why, for paths that would leave it.
    std::optional<std::filesystem::path> Resolve(std::string_view path) const;

    std::filesystem::path m_directory;
};

} // namespace Spot::Editor
