#include "spot/audio/audio_settings.h"

#include "spot/audio/audio_device.h"
#include "spot/core/io/archive.h"

namespace Spot {

void AudioBusSettings::Serialize(Archive& archive) {
    archive.Field("name", name);
    archive.Field("parent", parent, {.tooltip = "The bus it passes its sounds to; the master bus when empty"});
    archive.Field("volume", volume, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("muted", muted);
}

AudioSettings AudioSettings::MakeDefault() {
    return {.buses = {{.name = "Music"}, {.name = "Effects"}, {.name = "Ambience"}, {.name = "Interface"}}};
}

void AudioSettings::Serialize(Archive& archive) {
    archive.Field("masterVolume", masterVolume, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    // Tools edit the buses with a mixer of their own.
    archive.Field("buses", buses, {.hidden = true});
}

void AudioSettings::Apply(AudioDevice& device) const {
    device.SetMasterVolume(masterVolume);
    // Every bus first, so parents are found whatever their order.
    for (const AudioBusSettings& settings : buses) {
        if (!settings.name.empty() && !device.FindBus(settings.name)) {
            device.AddBus(settings.name);
        }
    }
    for (const AudioBusSettings& settings : buses) {
        const std::optional<AudioBusId> bus = settings.name.empty() ? std::nullopt : device.FindBus(settings.name);
        if (!bus || *bus == AudioBusId::Master) {
            continue;
        }
        const AudioBusId parent =
            settings.parent.empty() ? AudioBusId::Master : device.FindBus(settings.parent).value_or(AudioBusId::Master);
        // A parent that would make a loop is refused, and the bus stays where it was.
        device.SetBusParent(*bus, parent);
        device.SetBusVolume(*bus, settings.volume);
        device.SetBusMuted(*bus, settings.muted);
    }
}

} // namespace Spot
