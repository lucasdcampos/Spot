#pragma once

#include "gameplay/hud/hud.h"

#include <spot.h>

namespace Gameplay {

// The vehicle driven, in the place of what is in hand: its name a moment after getting in, its first gauge large, the
// unit of that one and the others beside it, as "km/h   3", and its controls. It knows no kind of vehicle: what it
// shows is what the vehicle describes (Vehicle::DescribeTelemetry() and DescribeHints()).
class VehicleElement : public HudElement {
public:
    Spot::EntityId name = Spot::EntityId::Invalid;
    Spot::EntityId icon = Spot::EntityId::Invalid;
    Spot::EntityId count = Spot::EntityId::Invalid;
    Spot::EntityId detail = Spot::EntityId::Invalid;
    Spot::EntityId hints = Spot::EntityId::Invalid;
    Spot::EntityId reloadTrack = Spot::EntityId::Invalid;
    float nameTime = 2.5f;
    Spot::Color accent = {1.0f, 0.68f, 0.22f, 1.0f};
    Spot::Color textColor = {0.8070f, 0.8228f, 0.8469f, 1.0f};
    float countSize = 28.0f;

    VehicleElement() { showWhen = {"InVehicle"}; }

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    bool m_wasShown = false;
    float m_nameLeft = 0.0f;
};

} // namespace Gameplay
