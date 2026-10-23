#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Spot {

class Archive;
class AudioDevice;

struct AudioBusSettings {
    std::string name;
    // The bus it passes its sounds to, by name; the master bus when empty, or when no bus before it has that name.
    std::string parent = {};
    float volume = 1.0f;
    bool muted = false;

    void Serialize(Archive& archive);
};

// The audio section of a game's settings: the mixer's buses and their volumes, which the application gives its audio
// device when it starts.
struct AudioSettings {
    static constexpr std::string_view SectionName = "audio";

    float masterVolume = 1.0f;
    std::vector<AudioBusSettings> buses = {};

    // What new projects start with: buses for music, effects, ambience and the interface.
    static AudioSettings MakeDefault();

    void Serialize(Archive& archive);
    // Adds the buses the device does not have yet, and gives every one its parent, volume and muting. Buses the
    // device has that the settings do not name stay.
    void Apply(AudioDevice& device) const;
};

} // namespace Spot
