#pragma once

#include "build/code_build.h"
#include "project/project.h"

#include <spot.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// What runs a project's game: an executable and its arguments.
struct GameLaunch {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
};

// The executable that a project's game runs with: the one built from its code, or the player for games without code.
std::filesystem::path GetGameExecutable(const Project& project, const std::filesystem::path& player);

// Readies the project's game to run from the project's imported assets, as the editor and `spot run` run it, and
// tells how to start it. A game with code runs its built executable, with the GameSettings written beside it; one
// without runs the player, told the folder in the project's cache directory where they are written. Returns null,
// after logging why, when the executable was not built or the settings cannot be written.
std::optional<GameLaunch> PrepareGameRun(const Project& project,
                                         const std::filesystem::path& player = Toolchain::GetBuilt().player);

// Where ExportGame() puts the project's game by default: builds/<platform> in the project's folder.
std::filesystem::path GetExportDirectory(const Project& project);

// Makes a folder with the game as it ships, from what the project last built and imported: its executable, named
// after the project, the engine library beside it, its imported assets and its GameSettings. Games without code ship
// the player as their executable, with what is beside it. What the folder held before is replaced. Returns false,
// after logging why, when the game was not built or the folder cannot be written.
bool ExportGame(const Project& project, const std::filesystem::path& directory,
                const std::filesystem::path& player = Toolchain::GetBuilt().player);

} // namespace Spot
