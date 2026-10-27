#pragma once

#include <spot.h>

#include <filesystem>
#include <string>

namespace Gameplay {

// What a player sets in the settings menu, kept in the user's data: look, volumes, graphics and rebound controls.
struct UserSettings {
    float sensitivity = 0.0025f;
    // Degrees.
    float fieldOfView = 60.0f;
    float masterVolume = 1.0f;
    float effectsVolume = 1.0f;
    float musicVolume = 0.7f;
    float ambienceVolume = 0.8f;
    float interfaceVolume = 1.0f;
    // 0 low, 1 medium, 2 high.
    int graphicsQuality = 2;
    // The action map's rebinds, as InputActionMap::SaveRebindsToString() writes them.
    std::string rebinds;

    void Serialize(Spot::Archive& archive);
    // Puts every value back in its range, or to its default when it is not a number.
    void Validate();
    Spot::SaveDataResult Save(const std::filesystem::path& directory) const;
    // Keeps the values as they were when the file cannot be loaded.
    Spot::SaveDataResult Load(const std::filesystem::path& directory);
    // Sets the volumes of the audio device's buses and the application's rendering, and gives the settings to the
    // UserSettingsListeners of each player's controller, pawn, occupant and held items, stopping at nested pawns.
    void Apply(Spot::Scene& scene) const;
    // Takes what the listeners and the audio device have now, before any settings are loaded.
    void Read(Spot::Scene& scene);
};

// A component of a player's controller or pawn that takes what the player set.
class UserSettingsListener {
public:
    virtual ~UserSettingsListener() = default;

    virtual void ApplySettings(const UserSettings& settings) = 0;
    // Gives what it has now, for the settings to start from.
    virtual void ReadSettings(UserSettings& /*settings*/) const {}
};

} // namespace Gameplay
