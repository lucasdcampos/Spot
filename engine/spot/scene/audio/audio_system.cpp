#include "spot/scene/audio/audio_system.h"

#include "spot/assets/asset_archive.h"
#include "spot/audio/audio_device.h"
#include "spot/core/io/archive.h"
#include "spot/scene/entity.h"
#include "spot/scene/rendering/camera.h"
#include "spot/scene/scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace Spot {

namespace {

AudioDevice* s_device = nullptr;

} // namespace

float AudioSpatialSettings::GetGain(float distance) const {
    const float near = std::max(minDistance, 0.0001f);
    const float far = std::max(maxDistance, near);
    if (distance <= near) {
        return 1.0f;
    }
    if (distance >= far) {
        return 0.0f;
    }
    if (rolloff == AudioRolloff::Linear) {
        return 1.0f - (distance - near) / (far - near);
    }
    return near / distance;
}

void AudioSpatialSettings::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 2> Rolloffs = {"Inverse", "Linear"};
    archive.Field("minDistance", minDistance,
                  {.min = 0.0f, .max = 10000.0f, .speed = 0.05f, .tooltip = "Heard at full volume up to it"});
    archive.Field("maxDistance", maxDistance,
                  {.min = 0.0f, .max = 10000.0f, .speed = 0.1f, .tooltip = "Silent past it"});
    archive.Field("rolloff", rolloff, {.enumNames = Rolloffs, .tooltip = "How the volume falls between the two"});
}

AudioSystem::~AudioSystem() {
    AudioDevice* device = Audio::GetDevice();
    if (!device) {
        return;
    }
    for (const OneShot& oneShot : m_oneShots) {
        device->Stop(oneShot.instance);
    }
    for (AudioSource* source : m_sources) {
        device->Stop(source->m_instance);
    }
}

SoundInstance AudioSystem::PlayOneShot(std::shared_ptr<const Sound> sound, Vector3 position,
                                       const OneShotParams& params) {
    AudioDevice* device = Audio::GetDevice();
    if (!device || !sound) {
        return {};
    }
    const auto [gain, pan] = Spatialize(position, params.spatialSettings);
    const SoundInstance instance = device->Play(std::move(sound), {.volume = params.volume * gain,
                                                                   .pan = pan,
                                                                   .pitch = params.pitch,
                                                                   .bus = Audio::FindBus(params.bus),
                                                                   .priority = params.priority,
                                                                   .maxInstances = params.maxInstances});
    if (instance == SoundInstance{}) {
        return instance;
    }
    if (m_paused) {
        device->SetPaused(instance, true);
    }
    m_oneShots.push_back({instance, position, params.volume, params.spatialSettings});
    return instance;
}

std::optional<AudioListenerPose> AudioSystem::FindListener() const {
    std::optional<AudioListenerPose> listener;
    GetScene().ForEachComponent<AudioListener>([&listener](const AudioListener& found) {
        if (!listener && found.IsActiveAndEnabled()) {
            const Transform& transform = found.GetEntity().GetTransform();
            listener = AudioListenerPose{transform.GetWorldPosition(), transform.GetWorldRotation()};
        }
    });
    if (listener) {
        return listener;
    }
    if (const Camera* camera = FindMainCamera(GetScene())) {
        const Transform& transform = camera->GetEntity().GetTransform();
        return AudioListenerPose{transform.GetWorldPosition(), transform.GetWorldRotation()};
    }
    return std::nullopt;
}

std::pair<float, float> AudioSystem::Spatialize(Vector3 position, const AudioSpatialSettings& settings) const {
    const std::optional<AudioListenerPose> listener = m_listener ? m_listener : FindListener();
    if (!listener) {
        return {1.0f, 0.0f};
    }
    const Vector3 offset = position - listener->position;
    const float distance = Length(offset);
    // Listeners face down -Z, as cameras do, so their right is +X.
    const Vector3 right = listener->rotation * Vector3{1.0f, 0.0f, 0.0f};
    const float pan = distance > 0.0001f ? std::clamp(Dot(offset / distance, right), -1.0f, 1.0f) : 0.0f;
    return {settings.GetGain(distance), pan};
}

void AudioSystem::SetPaused(bool paused) {
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    AudioDevice* device = Audio::GetDevice();
    if (!device) {
        return;
    }
    for (const OneShot& oneShot : m_oneShots) {
        device->SetPaused(oneShot.instance, paused);
    }
    for (const AudioSource* source : m_sources) {
        // Sources the game paused stay paused.
        if (!source->m_paused) {
            device->SetPaused(source->m_instance, paused);
        }
    }
}

void AudioSystem::OnLateUpdate(float /*deltaTime*/) {
    m_listener = FindListener();
    if (AudioDevice* device = Audio::GetDevice()) {
        std::erase_if(m_oneShots, [device](const OneShot& oneShot) { return !device->IsPlaying(oneShot.instance); });
        for (const OneShot& oneShot : m_oneShots) {
            const auto [gain, pan] = Spatialize(oneShot.position, oneShot.spatialSettings);
            device->SetVolume(oneShot.instance, oneShot.volume * gain);
            device->SetPan(oneShot.instance, pan);
        }
    }
    for (AudioSource* source : m_sources) {
        source->Refresh();
    }
    // Sounds started before the next frame find the listener anew.
    m_listener.reset();
}

void AudioSystem::Register(AudioSource& source) {
    if (std::ranges::find(m_sources, &source) == m_sources.end()) {
        m_sources.push_back(&source);
    }
}

void AudioSystem::Unregister(AudioSource& source) {
    std::erase(m_sources, &source);
}

void AudioSystem::OnEntityMoved(Entity& entity, Scene& source, Scene& destination) {
    if (&GetScene() == &source) {
        if (std::ranges::any_of(m_sources, [&](const AudioSource* sound) { return &sound->GetEntity() == &entity; })) {
            destination.GetOrAddSystem<AudioSystem>();
        }
    } else if (auto* previous = source.GetSystem<AudioSystem>()) {
        previous->MoveSources(entity, destination);
    }
}

void AudioSystem::MoveSources(const Entity& entity, Scene& destination) {
    std::erase_if(m_sources, [&](AudioSource* source) {
        if (&source->GetEntity() != &entity)
            return false;
        if (source->IsActiveAndEnabled()) {
            destination.GetOrAddSystem<Spot::AudioSystem>().Register(*source);
        }
        return true;
    });
}

namespace Audio {

void SetDevice(AudioDevice* device) {
    s_device = device;
}

AudioDevice* GetDevice() {
    return s_device;
}

AudioBusId FindBus(std::string_view name) {
    if (name.empty() || !s_device) {
        return AudioBusId::Master;
    }
    return s_device->FindBus(name).value_or(AudioBusId::Master);
}

SoundInstance PlayOneShot(Scene& scene, std::shared_ptr<const Sound> sound, Vector3 position, std::string_view bus) {
    return scene.GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(std::move(sound), position, {.bus = bus});
}

} // namespace Audio

} // namespace Spot
