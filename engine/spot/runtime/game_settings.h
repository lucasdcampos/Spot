#pragma once

#include "spot/audio/audio_settings.h"
#include "spot/core/asset_id.h"
#include "spot/platform/input_settings.h"
#include "spot/scene/physics/physics_settings.h"
#include "spot/scene/rendering/rendering_settings.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

class Archive;
class Scene;

// The settings of the engine's features, in sections such as physics, which projects store and their games read.
// Each section is a struct with a SectionName and a Serialize().
struct SettingsSections {
    PhysicsSettings physics = {};
    InputSettings input = {};
    AudioSettings audio = {};
    RenderingSettings rendering = {};
    // The sections this build of the engine does not know, as JSON text by name, so saving them loses nothing.
    std::map<std::string, std::string, std::less<>> unknown = {};

    // Calls fn(name, section) for each section this build knows, in the order tools show them.
    template <typename F>
    void ForEach(F&& fn) {
        fn(PhysicsSettings::SectionName, physics);
        fn(InputSettings::SectionName, input);
        fn(AudioSettings::SectionName, audio);
        fn(RenderingSettings::SectionName, rendering);
    }

    // Reads the sections of a JSON object that holds them by name. Sections it does not have keep their values.
    // Returns false, changing nothing, when the text is not a JSON object.
    bool LoadFromString(std::string_view text);
    // A JSON object holding every section by name, unknown ones included.
    std::string SaveToString() const;

    // Applies the sections that concern scenes to one, as the application does to the scenes it loads.
    void Apply(Scene& scene) const;
};

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
    SettingsSections sections = {};

    // The fields above the sections.
    void Serialize(Archive& archive);

    // Null, after logging why, when the file cannot be read or holds no game settings.
    static std::optional<GameSettings> LoadFromFile(const std::filesystem::path& file);
    static std::optional<GameSettings> LoadFromString(std::string_view text);
    bool SaveToFile(const std::filesystem::path& file) const;
    std::string SaveToString() const;
};

} // namespace Spot
