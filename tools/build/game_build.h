#pragma once

#include "project/project.h"

#include <spot.h>

#include <filesystem>
#include <string_view>

namespace Spot {

// The platform games built here run on, as export folders are named: "windows", "linux" or "macos".
std::string_view GetPlatformName();

// Imports the project's source assets that changed into its import directory, as the editor does when they change.
// Returns how many were imported; what could not be is logged.
std::size_t ImportProjectAssets(const Project& project);

// Imports the project's source assets into "imported" in the folder, and writes the game's GameSettings there, so a
// game's executable in that folder runs the project's game, as the engine's own sandbox does. Imports are cached as
// in a project's import directory. Returns false, after logging why, when the folder cannot be written.
bool CookGame(const Project& project, const std::filesystem::path& directory);

// Writes the GameSettings beside the project's built executable, so it runs from the project's imported assets, as
// the editor and `spot run` run it. Returns false, after logging why, when the executable was not built or the file
// cannot be written.
bool WriteRunSettings(const Project& project);

// Where ExportGame() puts the project's game by default: builds/<platform> in the project's folder.
std::filesystem::path GetExportDirectory(const Project& project);

// Makes a folder with the game as it ships, from what the project last built and imported: its executable, named
// after the project, the engine library beside it, its imported assets and its GameSettings. What the folder held
// before is replaced. Returns false, after logging why, when the game was not built or the folder cannot be written.
bool ExportGame(const Project& project, const std::filesystem::path& directory);

} // namespace Spot
