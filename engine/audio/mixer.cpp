#include "audio/mixer.h"

#include "core/assert.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Spot::Detail {

Mixer::Mixer(std::uint32_t sampleRate) : m_sampleRate(sampleRate) {
    SPOT_ASSERT(sampleRate > 0, "The mixer needs a sample rate");
    m_voices.reserve(64);
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
    std::scoped_lock lock(m_mutex);
    return m_masterVolume;
}

void Mixer::SetMasterVolume(float volume) {
    std::scoped_lock lock(m_mutex);
    m_masterVolume = volume;
}

void Mixer::Mix(std::span<float> output) {
    SPOT_ASSERT(output.size() % 2 == 0, "Stereo output needs an even number of samples, not {}", output.size());
    std::ranges::fill(output, 0.0f);

    std::scoped_lock lock(m_mutex);
    for (Voice& voice : m_voices) {
        if (!voice.paused && !voice.ended) {
            MixVoice(voice, output);
        }
    }

    // Clipping keeps loud mixes within the range every output format can hold.
    const float masterVolume = std::max(m_masterVolume, 0.0f);
    for (float& sample : output) {
        sample = std::clamp(sample * masterVolume, -1.0f, 1.0f);
    }
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
