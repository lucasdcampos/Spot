#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Contract between the audio capture and the audio backend chosen with SPOT_AUDIO, as AudioOutput is for playback.
// Only engine sources use it and no backend type appears in it.
namespace Spot::Detail {

class AudioInput {
public:
    // Opens the recording device of that name, or the system's default when empty or not found, delivering mono
    // samples at the sample rate, paused. Returns null, after logging why, when there is no usable device.
    static std::unique_ptr<AudioInput> Open(std::string_view device, std::uint32_t sampleRate);
    // The names of the recording devices there are now.
    static std::vector<std::string> GetDeviceNames();

    // Stops recording before returning.
    ~AudioInput();

    AudioInput(const AudioInput&) = delete;
    AudioInput& operator=(const AudioInput&) = delete;

    // Records from the device or stops, keeping it open.
    bool SetRecording(bool recording);
    // Takes up to output.size() of the samples recorded so far, oldest first; returns how many.
    std::size_t Read(std::span<float> output);
    // Drops what was recorded and not read.
    void Clear();
    std::string GetDeviceName() const;

private:
    struct Impl;

    explicit AudioInput(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot::Detail
