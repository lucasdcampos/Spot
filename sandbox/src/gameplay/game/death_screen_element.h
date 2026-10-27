#pragma once

#include "gameplay/hud/hud.h"

#include <string>

namespace Gameplay {

// Shown while the pawn is dead, its timer counting the seconds until it comes back.
class DeathScreenElement : public HudElement {
public:
    Spot::EntityId timer = Spot::EntityId::Invalid;
    // What the timer says, with {} for the seconds.
    std::string timerText = "Respawning in {}";

    DeathScreenElement() { showWhen = {"Dead"}; }

protected:
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
