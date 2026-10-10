#include "audio/mixer.h"

#include "core/assert.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Spot::Detail {

namespace {

// The gains the volume and the pan give the left and right channels.
std::pair<float, float> GetGains(const PlayParams& params) {
    const float volume = std::max(params.volume, 0.0f);
    const float pan = std::clamp(params.pan, -1.0f, 1.0f);
    return {volume * std::min(1.0f - pan, 1.0f), volume * std::min(1.0f + pan, 1.0f)};
}

float StepToward(float value, float target, float step) {
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

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

    // Voices are in the order they started, so the first found is the oldest.
    const auto isActive = [](const Voice& voice) { return !voice.stopping && !voice.ended; };
    if (params.maxInstances > 0) {
        const auto sameSound = [&](const Voice& voice) { return isActive(voice) && voice.sound == sound; };
        if (std::ranges::count_if(m_voices, sameSound) >= params.maxInstances) {
            StopVoice(*std::ranges::find_if(m_voices, sameSound), 0.0f);
        }
    }
    if (std::ranges::count_if(m_voices, isActive) >= m_maxVoices) {
        Voice* victim = nullptr;
        for (Voice& voice : m_voices) {
            if (isActive(voice) && (!victim || voice.params.priority < victim->params.priority)) {
                victim = &voice;
            }
        }
        if (!victim || victim->params.priority > params.priority) {
            return {};
        }
        StopVoice(*victim, 0.0f);
    }

    const std::uint32_t id = m_nextId++;
    // The default instance must never match, even after the counter wraps around.
    if (m_nextId == 0) {
        m_nextId = 1;
    }
    const auto [leftGain, rightGain] = GetGains(params);
    m_voices.push_back({.sound = std::move(sound),
                        .id = id,
                        .params = params,
                        .leftGain = leftGain,
                        .rightGain = rightGain,
                        .fade = params.fadeIn > 0.0f ? 0.0f : 1.0f,
                        .fadeStep = params.fadeIn > 0.0f ? GetFadeStep(params.fadeIn) : 0.0f});
    if (!FindBusSlot(params.bus)) {
        m_voices.back().params.bus = AudioBusId::Master;
    }
    return {id};
}

void Mixer::Stop(SoundInstance instance, float fadeOut) {
    std::scoped_lock lock(m_mutex);
    if (Voice* voice = FindVoice(instance)) {
        StopVoice(*voice, fadeOut);
    }
}

void Mixer::StopAll() {
    std::scoped_lock lock(m_mutex);
    for (Voice& voice : m_voices) {
        if (!voice.stopping && !voice.ended) {
            StopVoice(voice, 0.0f);
        }
    }
}

int Mixer::GetMaxVoices() const {
    std::scoped_lock lock(m_mutex);
    return m_maxVoices;
}

void Mixer::SetMaxVoices(int count) {
    std::scoped_lock lock(m_mutex);
    m_maxVoices = std::max(count, 1);
}

int Mixer::GetVoiceCount() const {
    std::scoped_lock lock(m_mutex);
    return static_cast<int>(
        std::ranges::count_if(m_voices, [](const Voice& voice) { return !voice.stopping && !voice.ended; }));
}

void Mixer::StopVoice(Voice& voice, float fadeOut) {
    voice.stopping = true;
    // A paused voice is silent already.
    if (voice.paused) {
        voice.ended = true;
        return;
    }
    voice.fadeStep = -GetFadeStep(std::max(fadeOut, ClickFadeSeconds));
}

float Mixer::GetFadeStep(float seconds) const {
    return 1.0f / std::max(seconds * static_cast<float>(m_sampleRate), 1.0f);
}

bool Mixer::IsPlaying(SoundInstance instance) const {
    std::scoped_lock lock(m_mutex);
    return FindVoice(instance) != nullptr;
}

bool Mixer::IsPaused(SoundInstance instance) const {
    std::scoped_lock lock(m_mutex);
    const Voice* voice = FindVoice(instance);
    return voice && voice->pauseRequested;
}

void Mixer::SetPaused(SoundInstance instance, bool paused) {
    std::scoped_lock lock(m_mutex);
    Voice* voice = FindVoice(instance);
    if (!voice || voice->pauseRequested == paused) {
        return;
    }
    voice->pauseRequested = paused;
    const float step = GetFadeStep(ClickFadeSeconds);
    voice->fadeStep = paused ? -step : step;
    if (!paused) {
        voice->paused = false;
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
            Bus& bus = m_buses[slot];
            const std::span<float> samples = GetBusBuffer(slot, count);
            ApplyBusGain(bus, samples);
            const std::span<float> parent = GetBusBuffer(bus.parent, count);
            for (std::size_t i = 0; i < count; ++i) {
                parent[i] += samples[i];
            }
            measure(slot, samples);
        }
        // Clipping keeps loud mixes within the range every output format can hold.
        const std::span<float> mixed = GetBusBuffer(0, count);
        ApplyBusGain(m_buses[0], mixed);
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

void Mixer::ApplyBusGain(Bus& bus, std::span<float> samples) const {
    const float target = bus.muted ? 0.0f : std::max(bus.volume, 0.0f);
    const float step = GetFadeStep(ClickFadeSeconds);
    for (std::size_t i = 0; i + 1 < samples.size(); i += 2) {
        bus.gain = StepToward(bus.gain, target, step);
        samples[i] *= bus.gain;
        samples[i + 1] *= bus.gain;
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
    const auto it = std::ranges::find_if(
        m_voices, [&](const Voice& voice) { return voice.id == instance.id && !voice.stopping && !voice.ended; });
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
    const auto [leftTarget, rightTarget] = GetGains(voice.params);
    // A change of the whole range takes as long as a fade.
    const float rampStep = GetFadeStep(ClickFadeSeconds);

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

        if (voice.fadeStep != 0.0f) {
            voice.fade = std::clamp(voice.fade + voice.fadeStep, 0.0f, 1.0f);
            if (voice.fade == 0.0f) {
                voice.fadeStep = 0.0f;
                if (voice.stopping) {
                    voice.ended = true;
                    return;
                }
                if (voice.pauseRequested) {
                    voice.paused = true;
                    return;
                }
            } else if (voice.fade == 1.0f) {
                voice.fadeStep = 0.0f;
            }
        }
        voice.leftGain = StepToward(voice.leftGain, leftTarget, rampStep);
        voice.rightGain = StepToward(voice.rightGain, rightTarget, rampStep);

        // Linear interpolation between neighboring frames converts the sample rate and applies the pitch.
        const auto frame = static_cast<std::uint64_t>(voice.position);
        const auto t = static_cast<float>(voice.position - static_cast<double>(frame));
        const float left = std::lerp(sampleAt(frame, 0), sampleAt(frame + 1, 0), t);
        const float right = channelCount == 2 ? std::lerp(sampleAt(frame, 1), sampleAt(frame + 1, 1), t) : left;
        output[i] += left * voice.leftGain * voice.fade;
        output[i + 1] += right * voice.rightGain * voice.fade;
        voice.position += step;
    }

    if (!loop && voice.position >= static_cast<double>(frameCount)) {
        voice.ended = true;
    }
}

} // namespace Spot::Detail
