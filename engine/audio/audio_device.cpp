#include "audio/audio_device.h"

#include "audio/audio_output.h"
#include "audio/mixer.h"
#include "core/log.h"
#include "core/profiler.h"

#include <utility>

namespace Spot {

namespace {

// Common to most hardware, so the system rarely has to convert the mix.
constexpr std::uint32_t MixSampleRate = 48000;

} // namespace

AudioDevice::AudioDevice(const AudioConfig& config) : m_mixer(std::make_unique<Detail::Mixer>(MixSampleRate)) {
    if (!config.enabled) {
        return;
    }

    m_output = Detail::AudioOutput::Open(*m_mixer);
    if (m_output) {
        Log::GetEngineLogger().Debug("Audio output opened with '{}' at {} Hz", m_output->GetBackendName(),
                                     MixSampleRate);
    } else {
        Log::GetEngineLogger().Warning("No audio output is available; sounds will not be heard");
    }
}

AudioDevice::~AudioDevice() = default;

bool AudioDevice::HasOutput() const {
    return m_output != nullptr;
}

std::string_view AudioDevice::GetBackendName() const {
    return m_output ? m_output->GetBackendName() : std::string_view();
}

std::uint32_t AudioDevice::GetSampleRate() const {
    return m_mixer->GetSampleRate();
}

SoundInstance AudioDevice::Play(std::shared_ptr<const Sound> sound, const PlayParams& params) {
    SPOT_PROFILE_SCOPE("AudioDevice::Play");
    // Nothing would ever mix the sound, so it would never end; refusing it keeps IsPlaying() truthful.
    if (!m_output) {
        return {};
    }
    return m_mixer->Play(std::move(sound), params);
}

void AudioDevice::Stop(SoundInstance instance) {
    SPOT_PROFILE_SCOPE("AudioDevice::Stop");
    m_mixer->Stop(instance);
}

void AudioDevice::StopAll() {
    SPOT_PROFILE_SCOPE("AudioDevice::StopAll");
    m_mixer->StopAll();
}

bool AudioDevice::IsPlaying(SoundInstance instance) const {
    return m_mixer->IsPlaying(instance);
}

bool AudioDevice::IsPaused(SoundInstance instance) const {
    return m_mixer->IsPaused(instance);
}

void AudioDevice::SetPaused(SoundInstance instance, bool paused) {
    SPOT_PROFILE_SCOPE("AudioDevice::SetPaused");
    m_mixer->SetPaused(instance, paused);
}

void AudioDevice::SetVolume(SoundInstance instance, float volume) {
    SPOT_PROFILE_SCOPE("AudioDevice::SetVolume");
    m_mixer->SetVolume(instance, volume);
}

void AudioDevice::SetPan(SoundInstance instance, float pan) {
    SPOT_PROFILE_SCOPE("AudioDevice::SetPan");
    m_mixer->SetPan(instance, pan);
}

void AudioDevice::SetPitch(SoundInstance instance, float pitch) {
    SPOT_PROFILE_SCOPE("AudioDevice::SetPitch");
    m_mixer->SetPitch(instance, pitch);
}

float AudioDevice::GetMasterVolume() const {
    return m_mixer->GetMasterVolume();
}

void AudioDevice::SetMasterVolume(float volume) {
    SPOT_PROFILE_SCOPE("AudioDevice::SetMasterVolume");
    m_mixer->SetMasterVolume(volume);
}

std::optional<AudioBusId> AudioDevice::AddBus(std::string_view name, AudioBusId parent) {
    return m_mixer->AddBus(name, parent);
}

bool AudioDevice::RemoveBus(AudioBusId bus) {
    return m_mixer->RemoveBus(bus);
}

bool AudioDevice::HasBus(AudioBusId bus) const {
    return m_mixer->HasBus(bus);
}

std::optional<AudioBusId> AudioDevice::FindBus(std::string_view name) const {
    return m_mixer->FindBus(name);
}

std::vector<AudioBusId> AudioDevice::GetBuses() const {
    return m_mixer->GetBuses();
}

std::string AudioDevice::GetBusName(AudioBusId bus) const {
    return m_mixer->GetBusName(bus);
}

std::optional<AudioBusId> AudioDevice::GetBusParent(AudioBusId bus) const {
    return m_mixer->GetBusParent(bus);
}

bool AudioDevice::SetBusParent(AudioBusId bus, AudioBusId parent) {
    return m_mixer->SetBusParent(bus, parent);
}

float AudioDevice::GetBusVolume(AudioBusId bus) const {
    return m_mixer->GetBusVolume(bus);
}

void AudioDevice::SetBusVolume(AudioBusId bus, float volume) {
    m_mixer->SetBusVolume(bus, volume);
}

bool AudioDevice::IsBusMuted(AudioBusId bus) const {
    return m_mixer->IsBusMuted(bus);
}

void AudioDevice::SetBusMuted(AudioBusId bus, bool muted) {
    m_mixer->SetBusMuted(bus, muted);
}

AudioPeak AudioDevice::GetBusPeak(AudioBusId bus) const {
    return m_mixer->GetBusPeak(bus);
}

} // namespace Spot
