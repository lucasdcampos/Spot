#include "gameplay/vehicles/vehicle_passenger.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/vehicles/vehicle_seat.h"

namespace Gameplay {

SPOT_COMPONENT(VehiclePassenger);

VehicleSeat* VehiclePassenger::GetSeat() const {
    const Pawn* pawn = Pawn::Of(GetEntity());
    if (!pawn || !pawn->HasState("Passenger")) {
        return nullptr;
    }
    VehicleSeat* found = nullptr;
    GetScene().ForEachComponent<VehicleSeat>([&](VehicleSeat& seat) {
        if (!seat.driver && seat.GetOccupant() == pawn) {
            found = &seat;
        }
    });
    return found;
}

void VehiclePassenger::OnPlayerInput(PlayerController& player) {
    VehicleSeat* seat = GetSeat();
    if (!seat) {
        m_context.Deactivate();
        return;
    }
    if (!m_context.Takes(seat->exitAction)) {
        m_context.SetActions({seat->exitAction});
    }
    m_context.Activate(&player);
    if (m_context.IsPressed(seat->exitAction)) {
        m_context.Consume(seat->exitAction);
        if (Pawn* pawn = Pawn::Of(GetEntity())) {
            pawn->Request("Gameplay.ExitVehicle");
        }
    }
}

void VehiclePassenger::OnPlayerRequest(std::string_view request) {
    if (request == "Gameplay.ExitVehicle") {
        if (VehicleSeat* seat = GetSeat()) {
            seat->Exit();
        }
    }
}

void VehiclePassenger::OnDisable() {
    m_context.Deactivate();
    if (VehicleSeat* seat = GetSeat()) {
        seat->Exit();
    }
}

} // namespace Gameplay
