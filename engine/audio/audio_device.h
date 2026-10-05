#pragma once

#include "audio/playback.h"
#include "audio/sound.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace Spot {

namespace Detail {
class AudioOutput;
class Mixer;
} // namespace Detail

struct AudioConfig {
    // Turned off for runs that must stay silent, such as tests or servers.
    bool enabled = true;
};

// Plays sounds on the default output device, mixed by the engine. Every method may be called from any thread.
class AudioDevice {
public:
    // Never fails: without an output, because audio is disabled or no device can be opened, the device stays usable
    // but plays nothing.
    explicit AudioDevice(const AudioConfig& config = {});
    ~AudioDevice();

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    bool HasOutput() const;
    // The system audio API in use, such as "wasapi", "coreaudio" or "pipewire"; empty without an output.
    std::string_view GetBackendName() const;
    // The rate the engine mixes at; sounds at other rates are converted while they play.
    std::uint32_t GetSampleRate() const;

    // The device keeps the sound alive while it plays. Without an output, or with a null sound, it returns the
    // default instance, which never plays.
    SoundInstance Play(std::shared_ptr<const Sound> sound, const PlayParams& params = {});
    void Stop(SoundInstance instance);
    void StopAll();

    // Paused playbacks still count as playing; ended and stopped ones do not.
    bool IsPlaying(SoundInstance instance) const;
    bool IsPaused(SoundInstance instance) const;
    void SetPaused(SoundInstance instance, bool paused);

    void SetVolume(SoundInstance instance, float volume);
    void SetPan(SoundInstance instance, float pan);
    void SetPitch(SoundInstance instance, float pitch);

    // Scales every sound; 1 by default.
    float GetMasterVolume() const;
    void SetMasterVolume(float volume);

private:
    // Declared before the output, so the output stops pulling from the mixer before the mixer is destroyed.
    std::unique_ptr<Detail::Mixer> m_mixer;
    std::unique_ptr<Detail::AudioOutput> m_output;
};

} // namespace Spot
