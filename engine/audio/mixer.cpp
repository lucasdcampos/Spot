#include "audio/mixer.h"

#include "core/assert.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Spot::Detail {

namespace {

std::size_t ToSlot(AudioBusId bus) {
    return static_cast<std::size_t>(bus);
}

AudioBusId ToBus(std::size_t slot) {
    return static_cast<AudioBusId>(slot);
}

} // namespace

Mixer::Mixer(std::uint32_t sampleRate) : m_sampleRate(sampleRate) {
    SPOT_ASSERT(sampleRate > 0, "The mixer needs a sample rate");
    m_voices.reserve(64);
    m_buses[0] = {.used = true, .name = "Master"};
    m_busOrder.reserve(MaxBuses);
    m_busBuffers.resize(MaxBuses * ChunkFrames * 2);
    for (std::atomic<float>& peak : m_peaks) {
        peak.store(0.0f, std::memory_order_relaxed);
    }
}

SoundInstance Mixer::Play(std::shared_ptr<const Sound> sound, const PlayParams& params) {
    if (!sound) {
        return {};
    }

    std::scoped_lock lock(m_mutex);
    std::erase_if(m_voices, [](const Voice& voice) { return voice.ended; });

    const std::uint32_t id = m_nextId++;
    // The default instance must never match, even after the counter wraps around.
    if (m_nextId == 0) {
        m_nextId = 1;
    }
    m_voices.push_back({.sound = std::move(sound), .id = id, .params = params});
    if (!FindBusSlot(params.bus)) {
        m_voices.back().params.bus = AudioBusId::Master;
    }
    return {id};
}

void Mixer::Stop(SoundInstance instance) {
    std::scoped_lock lock(m_mutex);
    std::erase_if(m_voices, [&](const Voice& voice) { return voice.id == instance.id; });
}

void Mixer::StopAll() {
    std::scoped_lock lock(m_mutex);
    m_voices.clear();
}

bool Mixer::IsPlaying(SoundInstance instance) const {
    std::scoped_lock lock(m_mutex);
    return FindVoice(instance) != nullptr;
}

bool Mixer::IsPaused(SoundInstance instance) const {
    std::scoped_lock lock(m_mutex);
    const Voice* voice = FindVoice(instance);
    return voice && voice->paused;
}

void Mixer::SetPaused(SoundInstance instance, bool paused) {
    std::scoped_lock lock(m_mutex);
    if (Voice* voice = FindVoice(instance)) {
        voice->paused = paused;
    }
}

void Mixer::SetVolume(SoundInstance instance, float volume) {
    std::scoped_lock lock(m_mutex);
    if (Voice* voice = FindVoice(instance)) {
        voice->params.volume = volume;
    }
}

void Mixer::SetPan(SoundInstance instance, float pan) {
    std::scoped_lock lock(m_mutex);
    if (Voice* voice = FindVoice(instance)) {
        voice->params.pan = pan;
    }
}

void Mixer::SetPitch(SoundInstance instance, float pitch) {
    std::scoped_lock lock(m_mutex);
    if (Voice* voice = FindVoice(instance)) {
        voice->params.pitch = pitch;
    }
}

float Mixer::GetMasterVolume() const {
    return GetBusVolume(AudioBusId::Master);
}

void Mixer::SetMasterVolume(float volume) {
    SetBusVolume(AudioBusId::Master, volume);
}

std::optional<AudioBusId> Mixer::AddBus(std::string_view name, AudioBusId parent) {
    std::scoped_lock lock(m_mutex);
    const auto free = std::ranges::find(m_buses, false, &Bus::used);
    if (free == m_buses.end()) {
        return std::nullopt;
    }
    const auto slot = static_cast<std::size_t>(free - m_buses.begin());
    *free = {.used = true, .name = std::string(name), .parent = FindBusSlot(parent) ? ToSlot(parent) : 0};
    m_peaks[slot * 2].store(0.0f, std::memory_order_relaxed);
    m_peaks[slot * 2 + 1].store(0.0f, std::memory_order_relaxed);
    UpdateBusOrder();
    return ToBus(slot);
}

bool Mixer::RemoveBus(AudioBusId bus) {
    std::scoped_lock lock(m_mutex);
    const Bus* removed = FindBusSlot(bus);
    if (!removed || bus == AudioBusId::Master) {
        return false;
    }
    const std::size_t slot = ToSlot(bus);
    const std::size_t parent = removed->parent;
    for (Bus& child : m_buses) {
        if (child.used && child.parent == slot) {
            child.parent = parent;
        }
    }
    for (Voice& voice : m_voices) {
        if (voice.params.bus == bus) {
            voice.params.bus = ToBus(parent);
        }
    }
    m_buses[slot] = {};
    UpdateBusOrder();
    return true;
}

bool Mixer::HasBus(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    return FindBusSlot(bus) != nullptr;
}

std::optional<AudioBusId> Mixer::FindBus(std::string_view name) const {
    std::scoped_lock lock(m_mutex);
    for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
        if (m_buses[slot].used && m_buses[slot].name == name) {
            return ToBus(slot);
        }
    }
    return std::nullopt;
}

std::vector<AudioBusId> Mixer::GetBuses() const {
    std::scoped_lock lock(m_mutex);
    std::vector<AudioBusId> buses;
    for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
        if (m_buses[slot].used) {
            buses.push_back(ToBus(slot));
        }
    }
    return buses;
}

std::string Mixer::GetBusName(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const Bus* found = FindBusSlot(bus);
    return found ? found->name : std::string();
}

std::optional<AudioBusId> Mixer::GetBusParent(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const Bus* found = FindBusSlot(bus);
    if (!found || bus == AudioBusId::Master) {
        return std::nullopt;
    }
    return ToBus(found->parent);
}

bool Mixer::SetBusParent(AudioBusId bus, AudioBusId parent) {
    std::scoped_lock lock(m_mutex);
    Bus* found = FindBusSlot(bus);
    if (!found || bus == AudioBusId::Master || !FindBusSlot(parent)) {
        return false;
    }
    // Climbing from the parent must never reach the bus, or the tree would become a loop.
    for (std::size_t slot = ToSlot(parent);; slot = m_buses[slot].parent) {
        if (slot == ToSlot(bus)) {
            return false;
        }
        if (slot == 0) {
            break;
        }
    }
    found->parent = ToSlot(parent);
    UpdateBusOrder();
    return true;
}

float Mixer::GetBusVolume(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const Bus* found = FindBusSlot(bus);
    return found ? found->volume : 0.0f;
}

void Mixer::SetBusVolume(AudioBusId bus, float volume) {
    std::scoped_lock lock(m_mutex);
    if (Bus* found = FindBusSlot(bus)) {
        found->volume = volume;
    }
}

bool Mixer::IsBusMuted(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const Bus* found = FindBusSlot(bus);
    return found && found->muted;
}

void Mixer::SetBusMuted(AudioBusId bus, bool muted) {
    std::scoped_lock lock(m_mutex);
    if (Bus* found = FindBusSlot(bus)) {
        found->muted = muted;
    }
}

AudioPeak Mixer::GetBusPeak(AudioBusId bus) const {
    const std::size_t slot = ToSlot(bus);
    if (slot >= MaxBuses) {
        return {};
    }
    return {m_peaks[slot * 2].load(std::memory_order_relaxed), m_peaks[slot * 2 + 1].load(std::memory_order_relaxed)};
}

void Mixer::Mix(std::span<float> output) {
    SPOT_ASSERT(output.size() % 2 == 0, "Stereo output needs an even number of samples, not {}", output.size());
    std::ranges::fill(output, 0.0f);

    std::scoped_lock lock(m_mutex);
    std::array<AudioPeak, MaxBuses> peaks = {};
    const auto measure = [&peaks](std::size_t slot, std::span<const float> samples) {
        for (std::size_t i = 0; i + 1 < samples.size(); i += 2) {
            peaks[slot].left = std::max(peaks[slot].left, std::abs(samples[i]));
            peaks[slot].right = std::max(peaks[slot].right, std::abs(samples[i + 1]));
        }
    };

    for (std::size_t offset = 0; offset < output.size(); offset += ChunkFrames * 2) {
        const std::size_t count = std::min(ChunkFrames * 2, output.size() - offset);
        for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
            if (m_buses[slot].used) {
                std::ranges::fill(GetBusBuffer(slot, count), 0.0f);
            }
        }
        for (Voice& voice : m_voices) {
            if (!voice.paused && !voice.ended) {
                MixVoice(voice, GetBusBuffer(ToSlot(voice.params.bus), count));
            }
        }
        // Each bus scales what reached it and passes it up, after the buses below it did.
        for (const std::size_t slot : m_busOrder) {
            const Bus& bus = m_buses[slot];
            const float gain = bus.muted ? 0.0f : std::max(bus.volume, 0.0f);
            const std::span<float> samples = GetBusBuffer(slot, count);
            const std::span<float> parent = GetBusBuffer(bus.parent, count);
            for (std::size_t i = 0; i < count; ++i) {
                samples[i] *= gain;
                parent[i] += samples[i];
            }
            measure(slot, samples);
        }
        // Clipping keeps loud mixes within the range every output format can hold.
        const Bus& master = m_buses[0];
        const float gain = master.muted ? 0.0f : std::max(master.volume, 0.0f);
        const std::span<float> mixed = GetBusBuffer(0, count);
        for (std::size_t i = 0; i < count; ++i) {
            mixed[i] *= gain;
        }
        measure(0, mixed);
        for (std::size_t i = 0; i < count; ++i) {
            output[offset + i] = std::clamp(mixed[i], -1.0f, 1.0f);
        }
    }

    for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
        m_peaks[slot * 2].store(peaks[slot].left, std::memory_order_relaxed);
        m_peaks[slot * 2 + 1].store(peaks[slot].right, std::memory_order_relaxed);
    }
}

const Mixer::Bus* Mixer::FindBusSlot(AudioBusId bus) const {
    const std::size_t slot = ToSlot(bus);
    return slot < MaxBuses && m_buses[slot].used ? &m_buses[slot] : nullptr;
}

Mixer::Bus* Mixer::FindBusSlot(AudioBusId bus) {
    return const_cast<Bus*>(std::as_const(*this).FindBusSlot(bus));
}

void Mixer::UpdateBusOrder() {
    std::array<int, MaxBuses> depths = {};
    m_busOrder.clear();
    for (std::size_t slot = 1; slot < MaxBuses; ++slot) {
        if (!m_buses[slot].used) {
            continue;
        }
        for (std::size_t up = slot; up != 0; up = m_buses[up].parent) {
            ++depths[slot];
        }
        m_busOrder.push_back(slot);
    }
    std::ranges::stable_sort(m_busOrder, [&depths](std::size_t a, std::size_t b) { return depths[a] > depths[b]; });
}

std::span<float> Mixer::GetBusBuffer(std::size_t slot, std::size_t samples) {
    return std::span<float>(m_busBuffers).subspan(slot * ChunkFrames * 2, samples);
}

Mixer::Voice* Mixer::FindVoice(SoundInstance instance) {
    return const_cast<Voice*>(std::as_const(*this).FindVoice(instance));
}

const Mixer::Voice* Mixer::FindVoice(SoundInstance instance) const {
    const auto it =
        std::ranges::find_if(m_voices, [&](const Voice& voice) { return voice.id == instance.id && !voice.ended; });
    return it != m_voices.end() ? &*it : nullptr;
}

void Mixer::MixVoice(Voice& voice, std::span<float> output) const {
    const Sound& sound = *voice.sound;
    const std::span<const float> samples = sound.GetSamples();
    const std::uint64_t frameCount = sound.GetFrameCount();
    const std::uint32_t channelCount = sound.GetChannelCount();
    const bool loop = voice.params.loop;
    if (frameCount == 0) {
        voice.ended = true;
        return;
    }

    const double step = static_cast<double>(sound.GetSampleRate()) / static_cast<double>(m_sampleRate) *
                        static_cast<double>(std::max(voice.params.pitch, 0.0f));
    const float volume = std::max(voice.params.volume, 0.0f);
    const float pan = std::clamp(voice.params.pan, -1.0f, 1.0f);
    const float leftGain = volume * std::min(1.0f - pan, 1.0f);
    const float rightGain = volume * std::min(1.0f + pan, 1.0f);

    // Past the end, a looping sound continues from its start and any other sound is silent.
    const auto sampleAt = [&](std::uint64_t frame, std::uint32_t channel) {
        if (frame >= frameCount) {
            if (!loop) {
                return 0.0f;
            }
            frame %= frameCount;
        }
        return samples[frame * channelCount + channel];
    };

    for (std::size_t i = 0; i + 1 < output.size(); i += 2) {
        if (voice.position >= static_cast<double>(frameCount)) {
            if (!loop) {
                break;
            }
            voice.position = std::fmod(voice.position, static_cast<double>(frameCount));
        }

        // Linear interpolation between neighboring frames converts the sample rate and applies the pitch.
        const auto frame = static_cast<std::uint64_t>(voice.position);
        const auto t = static_cast<float>(voice.position - static_cast<double>(frame));
        const float left = std::lerp(sampleAt(frame, 0), sampleAt(frame + 1, 0), t);
        const float right = channelCount == 2 ? std::lerp(sampleAt(frame, 1), sampleAt(frame + 1, 1), t) : left;
        output[i] += left * leftGain;
        output[i + 1] += right * rightGain;
        voice.position += step;
    }

    if (!loop && voice.position >= static_cast<double>(frameCount)) {
        voice.ended = true;
    }
}

} // namespace Spot::Detail
