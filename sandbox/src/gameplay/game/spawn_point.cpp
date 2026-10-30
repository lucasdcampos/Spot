#include "gameplay/game/spawn_point.h"

namespace Gameplay {

SPOT_COMPONENT(SpawnPoint);

void SpawnPoint::Serialize(Spot::Archive& archive) {
    archive.Field("tag", tag, {.tooltip = "The modes or teams that use it; empty for any"});
}

} // namespace Gameplay
