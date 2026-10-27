#pragma once

#include "gameplay/hud/hud.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// The crosshair, whose "Top", "Bottom", "Left" and "Right" lines move out as the aim gets less steady: moving, in the
// air and as recoil kicks. It goes while the pawn is in a state of hiddenBy, as down the sights, where the sights aim,
// or while nothing in hand can be used, unless it is also in one of shownBy, as placing what the spawn menu gave.
class CrosshairElement : public HudElement {
public:
    std::vector<std::string> hiddenBy = {"Aiming", "Sprinting", "Dead", "Choosing"};
    std::vector<std::string> shownBy = {"Placing"};

    CrosshairElement() { hideWhen = {"InVehicle"}; }

    float GetSpread() const { return m_spread; }

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_spread = 0.0f;
};

} // namespace Gameplay
