#pragma once

#include "spot/audio/playback.h"
#include "spot/audio/sound.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

namespace Detail {
class AudioOutput;
class Mixer;
} // namespace Detail

struct AudioConfig {
    // Turned off for runs that must stay silent, such as tests or servers.
    bool enabled = true;
    // Opens no output: sounds play as if heard, and whoever owns the device pulls the mix with Render(), as tests and
    // offline rendering do.
    bool manualMix = false;
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
    // With AudioConfig::manualMix, mixes the next frames into the output, as interleaved stereo at GetSampleRate();
    // otherwise does nothing, as the output pulls them.
    void Render(std::span<float> output);
    // The system audio API in use, such as "wasapi", "coreaudio" or "pipewire"; empty without an output.
    std::string_view GetBackendName() const;
    // The rate the engine mixes at; sounds at other rates are converted while they play.
    std::uint32_t GetSampleRate() const;

    // The device keeps the sound alive while it plays. Without an output, or with a null sound, it returns the
    // default instance, which never plays.
    SoundInstance Play(std::shared_ptr<const Sound> sound, const PlayParams& params = {});
    // Fades the sound out over the seconds, or a few milliseconds, so it never clicks. The instance stops matching at
    // once.
    void Stop(SoundInstance instance, float fadeOut = 0.0f);
    void StopAll();

    // How many sounds play at once at most, 64 by default; new ones beyond take voices or do not play (see
    // PlayParams::priority).
    int GetMaxVoices() const;
    void SetMaxVoices(int count);
    // The sounds playing, paused ones included.
    int GetVoiceCount() const;

    // Paused playbacks still count as playing; ended and stopped ones do not.
    bool IsPlaying(SoundInstance instance) const;
    bool IsPaused(SoundInstance instance) const;
    void SetPaused(SoundInstance instance, bool paused);

    void SetVolume(SoundInstance instance, float volume);
    void SetPan(SoundInstance instance, float pan);
    void SetPitch(SoundInstance instance, float pitch);

    // Scales every sound: the master bus's volume, 1 by default.
    float GetMasterVolume() const;
    void SetMasterVolume(float volume);

    // Buses group sounds, such as music, effects and voices, so each group's volume and muting can change at once.
    // They form a tree under the master bus: a sound plays into its bus (PlayParams::bus), each bus scales and mutes
    // what reaches it, and passes it to its parent. There are at most 32, the master one included.
    //
    // A new bus below the parent, or the master bus when the parent does not exist; nothing once there are 32. The ids
    // of removed buses go to new ones.
    std::optional<AudioBusId> AddBus(std::string_view name, AudioBusId parent = AudioBusId::Master);
    // Its sounds and its buses go to its parent. The master bus stays.
    bool RemoveBus(AudioBusId bus);
    bool HasBus(AudioBusId bus) const;
    std::optional<AudioBusId> FindBus(std::string_view name) const;
    // Every bus, the master one first.
    std::vector<AudioBusId> GetBuses() const;
    std::string GetBusName(AudioBusId bus) const;
    void SetBusName(AudioBusId bus, std::string_view name);
    // Nothing for the master bus and for buses that do not exist.
    std::optional<AudioBusId> GetBusParent(AudioBusId bus) const;
    // False, changing nothing, when either bus does not exist, the bus is the master one, or the parent is the bus or
    // below it.
    bool SetBusParent(AudioBusId bus, AudioBusId parent);
    float GetBusVolume(AudioBusId bus) const;
    void SetBusVolume(AudioBusId bus, float volume);
    bool IsBusMuted(AudioBusId bus) const;
    void SetBusMuted(AudioBusId bus, bool muted);
    // While any bus is soloed, only sounds on soloed buses, or on buses below them, are heard, as when listening to
    // one group of sounds alone.
    bool IsBusSolo(AudioBusId bus) const;
    void SetBusSolo(AudioBusId bus, bool solo);
    // The loudest sample of each channel that left the bus in the last mix, after its volume, for meters. Cheap and
    // never waits for the audio thread.
    AudioPeak GetBusPeak(AudioBusId bus) const;

private:
    // Declared before the output, so the output stops pulling from the mixer before the mixer is destroyed.
    std::unique_ptr<Detail::Mixer> m_mixer;
    std::unique_ptr<Detail::AudioOutput> m_output;
    bool m_manualMix = false;
};

} // namespace Spot
