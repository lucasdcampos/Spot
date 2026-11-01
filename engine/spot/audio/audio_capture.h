#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

namespace Detail {
class AudioInput;
} // namespace Detail

struct AudioCaptureConfig {
    // Turned off for runs that must never record, such as tests or servers: the capture stays usable and records
    // nothing.
    bool enabled = true;
    // Opens no device: it records only what Push() gives it, as tests do.
    bool manual = false;
    // Of the mono samples it delivers, whatever the device records at.
    std::uint32_t sampleRate = 48000;
};

// Records mono audio from a microphone, for voices: the system's default recording device, or the one SetDevice()
// names. The device opens the first time recording starts, which is when the system may ask the player for
// permission, and stays open, paused, while not recording, so starting again is immediate.
//
// Never fails: without a device, because capture is disabled or none can be opened, Start() returns false and Read()
// finds nothing. Use it from one thread at a time.
class AudioCapture {
public:
    explicit AudioCapture(const AudioCaptureConfig& config = {});
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    // Records until Stop(), dropping what was recorded and not read before. False, after logging why, when there is
    // no device to record from; a manual capture always starts.
    bool Start();
    void Stop();
    bool IsCapturing() const { return m_capturing; }
    std::uint32_t GetSampleRate() const { return m_sampleRate; }

    // Takes up to output.size() of the samples recorded and not read yet, oldest first; returns how many.
    std::size_t Read(std::span<float> output);
    // The loudest of the samples the last Read() took, from 0 to 1 and more when it clips, for meters.
    float GetPeak() const { return m_peak; }

    // Manual captures only: as if the microphone recorded the samples while capturing; ignored otherwise.
    void Push(std::span<const float> samples);

    // The names of the recording devices there are now; empty without any, or when capture is disabled or manual.
    std::vector<std::string> GetDeviceNames() const;
    // The device to record from, by name; the system's default when empty or not found. The open device closes, and
    // a capture that was recording goes on from the new one.
    void SetDevice(std::string_view name);
    const std::string& GetDevice() const { return m_device; }
    // The device actually recorded from; empty before the first Start(), and without one.
    std::string GetDeviceName() const;

private:
    std::unique_ptr<Detail::AudioInput> m_input;
    std::string m_device;
    std::uint32_t m_sampleRate;
    bool m_enabled;
    bool m_manual;
    bool m_capturing = false;
    float m_peak = 0.0f;
    // Manual captures: what Push() gave and Read() has not taken, from m_pushedRead on.
    std::vector<float> m_pushed;
    std::size_t m_pushedRead = 0;
};

} // namespace Spot
