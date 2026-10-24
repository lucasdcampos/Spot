#include "spot/scene/audio/audio_source.h"

#include "spot/assets/asset_archive.h"
#include "spot/audio/audio_device.h"
#include "spot/core/io/archive.h"
#include "spot/scene/audio/audio_system.h"
#include "spot/scene/entity.h"
#include "spot/scene/scene.h"

#include <tuple>

namespace Spot {

void AudioSource::Play() {
    AudioDevice* device = Audio::GetDevice();
    if (!device || !sound) {
        return;
    }
    AudioSystem& system = GetScene().GetOrAddSystem<Spot::AudioSystem>();
    if (m_paused && device->IsPlaying(m_instance)) {
        m_paused = false;
        if (!system.IsPaused()) {
            device->SetPaused(m_instance, false);
        }
        return;
    }
    Stop();
    if (spatial) {
        std::tie(m_gain, m_pan) = system.Spatialize(GetEntity().GetTransform().GetWorldPosition(), spatialSettings);
    } else {
        m_gain = 1.0f;
        m_pan = 0.0f;
    }
    m_instance = device->Play(
        sound.GetShared(),
        {.volume = volume * m_gain, .pan = m_pan, .pitch = pitch, .loop = loop, .bus = Audio::FindBus(bus)});
    if (system.IsPaused()) {
        device->SetPaused(m_instance, true);
    }
}

void AudioSource::Stop() {
    if (AudioDevice* device = Audio::GetDevice()) {
        device->Stop(m_instance);
    }
    m_instance = {};
    m_paused = false;
}

void AudioSource::Pause() {
    AudioDevice* device = Audio::GetDevice();
    if (!device || !device->IsPlaying(m_instance)) {
        return;
    }
    m_paused = true;
    device->SetPaused(m_instance, true);
}

bool AudioSource::IsPlaying() const {
    const AudioDevice* device = Audio::GetDevice();
    return device && device->IsPlaying(m_instance);
}

bool AudioSource::IsPaused() const {
    return m_paused && IsPlaying();
}

void AudioSource::OnEnable() {
    GetScene().GetOrAddSystem<Spot::AudioSystem>().Register(*this);
    if (playOnStart) {
        Play();
    }
}

void AudioSource::OnDisable() {
    Stop();
    // Never made anew while the scene goes, which disables its components.
    if (auto* system = GetScene().GetSystem<AudioSystem>()) {
        system->Unregister(*this);
    }
}

void AudioSource::Serialize(Archive& archive) {
    archive.Field("sound", sound);
    archive.Field("bus", bus, {.tooltip = "The mixer's bus it plays on; the master bus when empty"});
    archive.Field("volume", volume, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("pitch", pitch, {.min = 0.01f, .max = 4.0f, .speed = 0.01f, .tooltip = "2 plays an octave higher"});
    archive.Field("loop", loop);
    archive.Field("playOnStart", playOnStart, {.tooltip = "Plays once it starts, and each time it is enabled"});
    archive.Field("spatial", spatial,
                  {.tooltip = "Heard from where it is by the listener, rather than everywhere as music is"});
    archive.Field("spatialSettings", spatialSettings);
}

void AudioSource::Refresh() {
    AudioDevice* device = Audio::GetDevice();
    if (!device || !device->IsPlaying(m_instance)) {
        return;
    }
    if (spatial) {
        std::tie(m_gain, m_pan) = GetScene().GetOrAddSystem<Spot::AudioSystem>().Spatialize(
            GetEntity().GetTransform().GetWorldPosition(), spatialSettings);
    } else {
        m_gain = 1.0f;
        m_pan = 0.0f;
    }
    device->SetVolume(m_instance, volume * m_gain);
    device->SetPan(m_instance, m_pan);
    device->SetPitch(m_instance, pitch);
}

} // namespace Spot
