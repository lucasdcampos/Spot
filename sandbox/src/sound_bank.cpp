#include "sound_bank.h"
#include <spot/scene/audio/audio_system.h>

SPOT_COMPONENT(SoundBank);

SoundBank* SoundBank::Find(Spot::Scene& scene) {
    SoundBank* found = nullptr;
    scene.ForEachComponent<SoundBank>([&found](SoundBank& bank) {
        if (!found) {
            found = &bank;
        }
    });
    return found;
}

void SoundBank::PlayClick(Spot::Scene& scene) {
    const SoundBank* bank = Find(scene);
    Spot::AudioDevice* device = Spot::Audio::GetDevice();
    if (bank && device && bank->click) {
        device->Play(bank->click.GetShared(), {.volume = 0.5f, .bus = Spot::Audio::FindBus("Interface")});
    }
}

void SoundBank::Serialize(Spot::Archive& archive) {
    archive.Field("click", click);
}
