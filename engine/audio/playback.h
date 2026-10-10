#pragma once

#include <cstdint>

namespace Spot {

// A bus of the mixer, which sounds play into; see AudioDevice::AddBus(). Every sound reaches the master bus.
enum class AudioBusId : std::uint32_t { Master = 0 };

// The loudest sample of each channel, from 0 to 1 and more when it clips.
struct AudioPeak {
    float left = 0.0f;
    float right = 0.0f;
};

struct PlayParams {
    // Multiplies the samples; 1 keeps the sound as recorded.
    float volume = 1.0f;
    // From -1, only the left speaker, to 1, only the right one.
    float pan = 0.0f;
    // Playback speed, which also shifts the tone: 2 plays twice as fast and an octave higher.
    float pitch = 1.0f;
    bool loop = false;
    AudioBusId bus = AudioBusId::Master;
};

// Refers to one playback of a sound. Once the playback ends, the instance stops matching anything, so keeping it
// around is safe: calls that take it simply do nothing. The default instance never matches.
struct SoundInstance {
    std::uint32_t id = 0;

    bool operator==(const SoundInstance&) const = default;
};

} // namespace Spot
