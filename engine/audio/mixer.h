#pragma once

#include "audio/playback.h"
#include "audio/sound.h"
#include "audio/sound_stream.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Spot::Detail {

// Mixes the sounds being played into stereo frames at a fixed sample rate. It is engine code rather than backend
// code, so every audio backend sounds the same and only has to deliver the frames to the speakers.
//
// Voices play into buses, which form a tree under the master bus: each bus scales and mutes what reaches it, and
// passes it to its parent. Buses live in a fixed number of slots, so mixing never allocates and their peak meters can
// be read without locks.
//
// Streamed sounds decode ahead on a thread of the mixer's, started with the first of them, which tests may do without
// and update the streams themselves with UpdateStreams().
//
// All methods are thread-safe: the game controls playback while the backend's audio thread calls Mix().
class Mixer {
public:
    static constexpr std::size_t MaxBuses = 32;

    explicit Mixer(std::uint32_t sampleRate, bool streamThread = true);
    ~Mixer();

    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    std::uint32_t GetSampleRate() const { return m_sampleRate; }

    // Short enough not to be heard as a fade, long enough to keep a sound that stops from clicking.
    static constexpr float ClickFadeSeconds = 0.005f;
    static constexpr int DefaultMaxVoices = 64;

    // A null sound returns the default instance, which never plays, as does a sound that finds no voice (see
    // PlayParams::priority). Voices for a bus that does not exist play on the master bus.
    SoundInstance Play(std::shared_ptr<const Sound> sound, const PlayParams& params = {});
    // Fades the sound out over the seconds, or ClickFadeSeconds when that is longer. The instance stops matching at
    // once.
    void Stop(SoundInstance instance, float fadeOut = 0.0f);
    void StopAll();

    // How many sounds play at once at most; new ones beyond take voices or do not play (see PlayParams::priority).
    int GetMaxVoices() const;
    void SetMaxVoices(int count);
    // The sounds playing, paused ones included.
    int GetVoiceCount() const;

    // Paused playbacks still count as playing; ended and stopped ones do not. Pausing and resuming fade, as stopping
    // does, and volume and pan changes ramp over as long, so none of them click.
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
    void SetBusName(AudioBusId bus, std::string_view name);
    // Nothing for the master bus, which has no parent, and for buses that do not exist.
    std::optional<AudioBusId> GetBusParent(AudioBusId bus) const;
    // False, changing nothing, when either bus does not exist, the bus is the master one, or the parent is the bus or
    // below it.
    bool SetBusParent(AudioBusId bus, AudioBusId parent);
    // Volume and muting changes ramp, as those of sounds do.
    float GetBusVolume(AudioBusId bus) const;
    void SetBusVolume(AudioBusId bus, float volume);
    bool IsBusMuted(AudioBusId bus) const;
    void SetBusMuted(AudioBusId bus, bool muted);
    // While any bus is soloed, only sounds on soloed buses, or on buses below them, are heard.
    bool IsBusSolo(AudioBusId bus) const;
    void SetBusSolo(AudioBusId bus, bool solo);
    // The loudest sample of each channel that left the bus in the last mix, after its volume; read without locks.
    AudioPeak GetBusPeak(AudioBusId bus) const;

    // Decodes ahead for every streamed playback, as the mixer's thread does.
    void UpdateStreams();

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
        // What was asked for; the voice goes silent and stops moving once its fade out ends.
        bool pauseRequested = false;
        bool paused = false;
        bool stopping = false;
        bool ended = false;
        // Streamed sounds only: the decoded frames played between, the first of which is the stream's frame of that
        // index, and how many silent frames followed the end.
        std::shared_ptr<SoundStream> stream;
        std::array<float, 2> frame0 = {};
        std::array<float, 2> frame1 = {};
        std::uint64_t streamFrame = 0;
        int streamTail = 0;
        // The gains reached so far, which follow the volume and the pan a step each frame.
        float leftGain = 0.0f;
        float rightGain = 0.0f;
        // From 0, silent, to 1, changed by the step each frame while fading.
        float fade = 1.0f;
        float fadeStep = 0.0f;
    };

    struct Bus {
        bool used = false;
        std::string name;
        std::size_t parent = 0;
        float volume = 1.0f;
        bool muted = false;
        bool solo = false;
        // Silenced by another bus's solo; see UpdateBusOrder().
        bool soloSilenced = false;
        // The gain reached so far, which follows the volume and muting a step each frame.
        float gain = 1.0f;
    };

    Voice* FindVoice(SoundInstance instance);
    const Voice* FindVoice(SoundInstance instance) const;
    // The gain multiplies the voice's volume, as solo does.
    void MixVoice(Voice& voice, std::span<float> output, float gain) const;
    // Fades and ramps the voice a frame; false once a fade silenced it.
    bool StepEnvelope(Voice& voice, float leftTarget, float rightTarget) const;
    // The streamed voice's frame at its position; false once past the end.
    static bool ReadStreamFrame(Voice& voice, std::array<float, 2>& frame);
    static void AdvanceStream(Voice& voice);
    void RunStreamThread();
    void StopVoice(Voice& voice, float fadeOut);
    float GetFadeStep(float seconds) const;
    void ApplyBusGain(Bus& bus, std::span<float> samples) const;
    const Bus* FindBusSlot(AudioBusId bus) const;
    Bus* FindBusSlot(AudioBusId bus);
    // Orders the buses so each comes before its parent, as mixing passes them up, and works out which solo silences.
    void UpdateBusOrder();
    std::span<float> GetBusBuffer(std::size_t slot, std::size_t samples);

    std::uint32_t m_sampleRate;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::uint32_t m_nextId = 1;
    int m_maxVoices = DefaultMaxVoices;
    // By slot, which is the bus's id; the master bus is the first.
    std::array<Bus, MaxBuses> m_buses;
    // The buses but the master one, deepest first.
    std::vector<std::size_t> m_busOrder;
    std::vector<float> m_busBuffers;
    std::array<std::atomic<float>, MaxBuses * 2> m_peaks;

    // Apart from the mixer's lock, so decoding never holds up mixing.
    std::mutex m_streamsMutex;
    std::vector<std::weak_ptr<SoundStream>> m_streams;
    bool m_useStreamThread;
    bool m_stopStreams = false;
    std::condition_variable m_streamsChanged;
    std::thread m_streamThread;
};

} // namespace Spot::Detail
