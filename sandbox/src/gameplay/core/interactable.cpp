#include "gameplay/core/interactable.h"

namespace Gameplay {

void Interactable::Serialize(Spot::Archive& archive) {
    archive.Field("prompt", prompt, {.tooltip = "What the prompt says using it does"});
}

} // namespace Gameplay
