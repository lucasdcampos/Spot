#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/core/player_input.h"

#include <spot.h>

namespace Gameplay {

class VehicleSeat;

// On a pawn that can ride without driving. It keeps its own controller and view; only its seat can move it.
class VehiclePassenger : public Spot::Component, public PlayerInputListener {
public:
    VehicleSeat* GetSeat() const;

protected:
    bool IsInputBeforeFixedSteps() const override { return true; }
    void OnPlayerInput(PlayerController& player) override;
    void OnPlayerRequest(std::string_view request) override;
    void OnPlayerUnpossess() override { m_context.Deactivate(); }
    void OnDisable() override;

private:
    InputContext m_context{{.name = "Passenger", .priority = 50}};
};

} // namespace Gameplay
