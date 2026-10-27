#pragma once

#include "gameplay/hud/hud.h"

#include <spot.h>

namespace Gameplay {

// The time of the scene's DayCycle, as hours and minutes, with the sun beside it by day and the moon by night.
class ClockElement : public HudElement {
public:
    Spot::EntityId text = Spot::EntityId::Invalid;
    Spot::EntityId icon = Spot::EntityId::Invalid;
    Spot::AssetRef<Spot::Texture> sunIcon;
    Spot::AssetRef<Spot::Texture> moonIcon;

protected:
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
