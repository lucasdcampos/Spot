#include "spot/scene/scripting/script_module_bindings.h"

#include "spot/scene/audio/audio_source.h"

namespace Spot::Detail {

void RegisterAudioScriptBindings() {
    ScriptBindings<AudioSource>(BuiltinScriptBindingsTag{})
        .Property(
            "volume", [](const AudioSource& audio) { return audio.volume; },
            [](AudioSource& audio, float value) {
                if (value < 0)
                    throw std::runtime_error("Volume cannot be negative");
                audio.volume = value;
            })
        .Property(
            "pitch", [](const AudioSource& audio) { return audio.pitch; },
            [](AudioSource& audio, float value) {
                if (value <= 0)
                    throw std::runtime_error("Pitch must be positive");
                audio.pitch = value;
            })
        .Field("loop", &AudioSource::loop)
        .Field("spatial", &AudioSource::spatial)
        .Field("playOnStart", &AudioSource::playOnStart)
        .Field("bus", &AudioSource::bus)
        .ReadOnly("playing", &AudioSource::IsPlaying)
        .ReadOnly("paused", &AudioSource::IsPaused)
        .Method("Play", &AudioSource::Play)
        .Method("Pause", &AudioSource::Pause)
        .Method("Stop", &AudioSource::Stop);
    PropertyMethods<AudioSource>("playing", "IsPlaying");
    PropertyMethods<AudioSource>("paused", "IsPaused");
}

} // namespace Spot::Detail
