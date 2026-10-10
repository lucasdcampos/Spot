#pragma once

#include "audio/playback.h"
#include "audio/sound.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Detail {

// Mixes the sounds being played into stereo frames at a fixed sample rate. It is engine code rather than backend
// code, so every audio backend sounds the same and only has to deliver the frames to the speakers.
//
// Voices play into buses, which form a tree under the master bus: each bus scales and mutes what reaches it, and
// passes it to its parent. Buses live in a fixed number of slots, so mixing never allocates and their peak meters can
// be read without locks.
//
// All methods are thread-safe: the game controls playback while the backend's audio thread calls Mix().
class Mixer {
public:
    static constexpr std::size_t MaxBuses = 32;

    explicit Mixer(std::uint32_t sampleRate);

    std::uint32_t GetSampleRate() const { return m_sampleRate; }

    // A null sound returns the default instance, which never plays. Voices for a bus that does not exist play on the
    // master bus.
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

    // The master bus's volume.
    float GetMasterVolume() const;
    void SetMasterVolume(float volume);

    // A new bus below the parent, or the master bus when the parent does not exist. Returns nothing once every slot
    // is taken. The ids of removed buses go to new ones.
    std::optional<AudioBusId> AddBus(std::string_view name, AudioBusId parent = AudioBusId::Master);
    // Its voices and its buses go to its parent. The master bus stays.
    bool RemoveBus(AudioBusId bus);
    bool HasBus(AudioBusId bus) const;
    std::optional<AudioBusId> FindBus(std::string_view name) const;
    // Every bus, the master bus first.
    std::vector<AudioBusId> GetBuses() const;
    std::string GetBusName(AudioBusId bus) const;
    // Nothing for the master bus, which has no parent, and for buses that do not exist.
    std::optional<AudioBusId> GetBusParent(AudioBusId bus) const;
    // False, changing nothing, when either bus does not exist, the bus is the master one, or the parent is the bus or
    // below it.
    bool SetBusParent(AudioBusId bus, AudioBusId parent);
    float GetBusVolume(AudioBusId bus) const;
    void SetBusVolume(AudioBusId bus, float volume);
    bool IsBusMuted(AudioBusId bus) const;
    void SetBusMuted(AudioBusId bus, bool muted);
    // The loudest sample of each channel that left the bus in the last mix, after its volume; read without locks.
    AudioPeak GetBusPeak(AudioBusId bus) const;

    // Fills the output with interleaved stereo frames, replacing what it held. Called from the audio thread, so it
    // neither allocates nor frees: sounds that end are released by the next Play() instead.
    void Mix(std::span<float> output);

private:
    // Frames mixed at once into the buses' buffers.
    static constexpr std::size_t ChunkFrames = 256;

    struct Voice {
        std::shared_ptr<const Sound> sound;
        std::uint32_t id;
        PlayParams params;
        // In source frames; fractional because the pitch and the sample rate conversion step through the sound.
        double position = 0.0;
        bool paused = false;
        bool ended = false;
    };

    struct Bus {
        bool used = false;
        std::string name;
        std::size_t parent = 0;
        float volume = 1.0f;
        bool muted = false;
    };

    Voice* FindVoice(SoundInstance instance);
    const Voice* FindVoice(SoundInstance instance) const;
    void MixVoice(Voice& voice, std::span<float> output) const;
    const Bus* FindBusSlot(AudioBusId bus) const;
    Bus* FindBusSlot(AudioBusId bus);
    // Orders the buses so each comes before its parent, as mixing passes them up.
    void UpdateBusOrder();
    std::span<float> GetBusBuffer(std::size_t slot, std::size_t samples);

    std::uint32_t m_sampleRate;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::uint32_t m_nextId = 1;
    // By slot, which is the bus's id; the master bus is the first.
    std::array<Bus, MaxBuses> m_buses;
    // The buses but the master one, deepest first.
    std::vector<std::size_t> m_busOrder;
    std::vector<float> m_busBuffers;
    std::array<std::atomic<float>, MaxBuses * 2> m_peaks;
};

} // namespace Spot::Detail
