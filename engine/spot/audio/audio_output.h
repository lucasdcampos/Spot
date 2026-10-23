#pragma once

#include <memory>
#include <string_view>

// Contract between the audio device and the audio backend chosen with SPOT_AUDIO. Only engine sources use it and no
// backend type appears in it, so switching backends leaves the public API untouched. The backend only delivers what
// the engine's mixer produces, so every backend sounds the same.
namespace Spot::Detail {

class Mixer;

class AudioOutput {
public:
    // Opens the default playback device and starts pulling stereo frames at the mixer's sample rate from
    // Mixer::Mix() on the backend's audio thread. Returns null, after logging why, when there is no usable device.
    static std::unique_ptr<AudioOutput> Open(Mixer& mixer);

    // Stops the audio thread before returning, so the mixer may be destroyed right after.
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // The system audio API in use, such as "wasapi", "coreaudio" or "pipewire".
    std::string_view GetBackendName() const;

private:
    struct Impl;

    explicit AudioOutput(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot::Detail
