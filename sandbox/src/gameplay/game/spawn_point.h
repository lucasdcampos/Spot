#pragma once

#include <spot.h>

#include <string>

namespace Gameplay {

// Where a player may appear: its entity's position and facing. A game mode takes the spawn points of its tag and
// those without one, or all of them when it names none, so a map can have places for each mode or team.
class SpawnPoint final : public Spot::Component {
public:
    std::string tag;

    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
