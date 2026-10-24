#include "spot/scene/audio/audio_components.h"

#include "spot/scene/audio/audio_source.h"
#include "spot/scene/component_registry.h"
#include "spot/scene/entity.h"

namespace Spot {

void RegisterAudioComponents() {
    RegisterComponent<AudioSource>("AudioSource");
    RegisterComponent<AudioListener>("AudioListener");
}

} // namespace Spot
