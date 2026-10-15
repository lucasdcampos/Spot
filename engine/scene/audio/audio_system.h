#pragma once

#include "audio/playback.h"
#include "audio/sound.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "scene/audio/audio_source.h"
#include "scene/scene_system.h"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace Spot {

class AudioDevice;

struct OneShotParams {
    float volume = 1.0f;
    float pitch = 1.0f;
    // The bus by name; the master bus when empty or missing.
    std::string_view bus = {};
    AudioSpatialSettings spatialSettings = {};
    // See PlayParams::priority and PlayParams::maxInstances.
    int priority = 0;
    int maxInstances = 0;
};

// Where a scene hears from.
struct AudioListenerPose {
    Vector3 position = {};
    Quaternion rotation = {};
};

// The sounds of a scene: each frame, once everything moved, it works out how its listener hears every spatial sound
// of the scene, and gives the playing sounds their volume and pan. Created when the scene first needs it (see
// Scene::GetAudio()). Its sounds pause while the application pauses the scenes, and stop when the scene goes.
class AudioSystem final : public SceneSystem {
public:
    ~AudioSystem() override;

    // Plays the sound once where the position is, heard from the listener as a spatial AudioSource would be. Returns
    // the default instance without an audio device or a sound.
    SoundInstance PlayOneShot(std::shared_ptr<const Sound> sound, Vector3 position, const OneShotParams& params = {});

    // The scene's listener: the first enabled AudioListener on an active entity, else the main camera; nothing when
    // neither is there.
    std::optional<AudioListenerPose> FindListener() const;
    // How the listener hears a sound at the position: its gain after distance, and its pan, from -1, left, to 1.
    std::pair<float, float> Spatialize(Vector3 position, const AudioSpatialSettings& settings) const;

    // Pauses every sound of the scene, or resumes those the game did not pause itself; the application does it when
    // it pauses the scenes.
    void SetPaused(bool paused);
    bool IsPaused() const { return m_paused; }

protected:
    void OnLateUpdate(float deltaTime) override;

private:
    friend class AudioSource;
    friend class Scene;

    struct OneShot {
        SoundInstance instance;
        Vector3 position;
        float volume;
        AudioSpatialSettings spatialSettings;
    };

    AudioSystem() = default;

    void Register(AudioSource& source);
    void Unregister(AudioSource& source);

    std::vector<AudioSource*> m_sources;
    std::vector<OneShot> m_oneShots;
    // Worked out once a frame, for every sound of the frame.
    std::optional<AudioListenerPose> m_listener;
    bool m_paused = false;
};

// The audio device of the running application, which audio components play through.
namespace Audio {

// The application points this at its device. Without one, audio components play nothing.
void SetDevice(AudioDevice* device);
AudioDevice* GetDevice();
// The bus of the name on the device; the master bus when empty, missing or without a device.
AudioBusId FindBus(std::string_view name);

// Plays the sound once where the position is, in the scene; see AudioSystem::PlayOneShot().
SoundInstance PlayOneShot(Scene& scene, std::shared_ptr<const Sound> sound, Vector3 position,
                          std::string_view bus = {});

} // namespace Audio

} // namespace Spot
