#pragma once

#include "assets/asset_ref.h"
#include "audio/playback.h"
#include "audio/sound.h"
#include "scene/component.h"

#include <cstdint>
#include <string>

namespace Spot {

// How a spatial sound fades with distance: at full volume up to the min distance, silent past the max distance.
enum class AudioRolloff : std::uint8_t {
    // As sound does in the open: the volume halves each time the distance doubles past the min distance.
    Inverse,
    // Evenly from full to silent between the two distances, so the max distance is reached gently.
    Linear,
};

struct AudioSpatialSettings {
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    AudioRolloff rolloff = AudioRolloff::Inverse;

    // The volume at the distance, from 0 to 1.
    float GetGain(float distance) const;
    void Serialize(Archive& archive);
};

// Plays a sound from its entity: heard from the scene's listener (see AudioListener), louder near it and from its
// side, when spatial, or as it is, everywhere, when not, as music is. It plays on its bus, named as the mixer names it
// (see AudioDevice::FindBus()); the master bus when empty or missing. With playOnStart, it plays once the component
// starts, and again each time it is enabled. It stops when disabled or removed, and when its scene unloads, and
// pauses with the scenes.
class AudioSource final : public Component {
public:
    AssetRef<Sound> sound;
    std::string bus;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnStart = true;
    bool spatial = true;
    AudioSpatialSettings spatialSettings;

    // Plays the sound from its start, or goes on with it when paused. Does nothing without an audio device (see
    // Audio::GetDevice()) or a sound.
    void Play();
    void Stop();
    void Pause();
    bool IsPlaying() const;
    bool IsPaused() const;

    // What the scene's listener hears of it, as last worked out: its volume after distance, and its pan.
    float GetSpatialGain() const { return m_gain; }
    float GetSpatialPan() const { return m_pan; }

protected:
    void OnEnable() override;
    void OnDisable() override;
    void Serialize(Archive& archive) override;

private:
    friend class AudioSystem;

    // Works out the gain and pan, and gives them, the volume and the pitch to the playing sound.
    void Refresh();

    SoundInstance m_instance;
    bool m_paused = false;
    float m_gain = 1.0f;
    float m_pan = 0.0f;
};

// Where a scene's sounds are heard from: its entity's position and the way it faces. Without one, a scene hears from
// its main camera (see FindMainCamera()), and without either, spatial sounds play at full volume in the middle.
class AudioListener final : public Component {};

} // namespace Spot
