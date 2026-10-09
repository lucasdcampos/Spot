#pragma once

#include "assets/asset_id.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

class Archive;

// What a game is to the engine that runs it. Games read it from game.json beside their executable, which the tools
// write from the game's project when they build it; it is never edited by hand.
struct GameSettings {
    static constexpr std::string_view FileName = "game.json";

    // Also the window's title, unless the application gives it another.
    std::string name = {};
    // The game's own version, such as "1.2.0".
    std::string version = "0.1.0";
    // The scene the game starts with.
    AssetId startupScene = {};
    // Where the game's imported assets are, with '/' separators: relative to the executable's folder, as in builds
    // made to ship, or absolute, as when the tools run a game from its project's imports.
    std::string importDirectory = "imported";

    void Serialize(Archive& archive);

    // Null, after logging why, when the file cannot be read or holds no game settings.
    static std::optional<GameSettings> LoadFromFile(const std::filesystem::path& file);
    static std::optional<GameSettings> LoadFromString(std::string_view text);
    bool SaveToFile(const std::filesystem::path& file) const;
    std::string SaveToString() const;
};

} // namespace Spot
