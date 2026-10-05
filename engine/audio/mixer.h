#pragma once

#include "audio/playback.h"
#include "audio/sound.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace Spot::Detail {

// Mixes the sounds being played into stereo frames at a fixed sample rate. It is engine code rather than backend
// code, so every audio backend sounds the same and only has to deliver the frames to the speakers.
// All methods are thread-safe: the game controls playback while the backend's audio thread calls Mix().
class Mixer {
public:
    explicit Mixer(std::uint32_t sampleRate);

    std::uint32_t GetSampleRate() const { return m_sampleRate; }

    // A null sound returns the default instance, which never plays.
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

    float GetMasterVolume() const;
    void SetMasterVolume(float volume);

    // Fills the output with interleaved stereo frames, replacing what it held. Called from the audio thread, so it
    // neither allocates nor frees: sounds that end are released by the next Play() instead.
    void Mix(std::span<float> output);

private:
    struct Voice {
        std::shared_ptr<const Sound> sound;
        std::uint32_t id;
        PlayParams params;
        // In source frames; fractional because the pitch and the sample rate conversion step through the sound.
        double position = 0.0;
        bool paused = false;
        bool ended = false;
    };

    Voice* FindVoice(SoundInstance instance);
    const Voice* FindVoice(SoundInstance instance) const;
    void MixVoice(Voice& voice, std::span<float> output) const;

    std::uint32_t m_sampleRate;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::uint32_t m_nextId = 1;
    float m_masterVolume = 1.0f;
};

} // namespace Spot::Detail
