#include "gameplay/vehicles/vehicle.h"

#include "gameplay/core/player_controller.h"
#include "gameplay/vehicles/vehicle_seat.h"

#include <cmath>
#include <utility>

namespace Gameplay {

std::vector<std::string> VehicleControls::GetActions() const {
    std::vector<std::string> actions;
    for (const auto* action : {&accelerate, &brake, &steer, &handbrake, &ascend, &descend}) {
        if (!action->empty()) {
            actions.push_back(*action);
        }
    }
    return actions;
}

VehicleInput VehicleControls::Read(const InputContext& input) const {
    return {.drive = input.GetValue(accelerate) - input.GetValue(brake),
            .steer = input.GetVector2(steer).x,
            .handbrake = input.IsDown(handbrake),
            .lift = input.GetValue(ascend) - input.GetValue(descend)};
}

void VehicleControls::Serialize(Spot::Archive& archive) {
    archive.Field("accelerate", accelerate);
    archive.Field("brake", brake);
    archive.Field("steer", steer, {.tooltip = "Its X steers"});
    archive.Field("handbrake", handbrake);
    archive.Field("ascend", ascend);
    archive.Field("descend", descend);
}

VehicleSeat* Vehicle::GetSeat() const {
    return GetEntity().GetComponentInChildren<VehicleSeat>();
}

std::vector<Gauge> Vehicle::DescribeTelemetry() const {
    return {{"Speed", std::to_string(std::lround(std::abs(GetForwardSpeed()) * 3.6f)), "km/h"}};
}

std::vector<InputHint> Vehicle::DescribeHints() const {
    std::vector<InputHint> hints = {{controls.accelerate, "Drive"}, {controls.handbrake, "Handbrake"}};
    if (const VehicleSeat* seat = GetSeat()) {
        hints.push_back({seat->exitAction, "Exit"});
    }
    return hints;
}

void Vehicle::OnPossess(Controller& /*controller*/) {
    m_input = {};
    SetState("InVehicle", true);
}

void Vehicle::OnUnpossess() {
    m_input = {};
    m_context.Deactivate();
    SetState("InVehicle", false);
}

void Vehicle::OnPlayerInput(PlayerController& player) {
    VehicleSeat* seat = GetSeat();
    std::vector<std::string> actions = controls.GetActions();
    if (seat) {
        actions.push_back(seat->exitAction);
    }
    if (m_context.GetDesc().actions != actions) {
        m_context.SetActions(std::move(actions));
    }
    m_context.Activate(&player);
    m_input = controls.Read(m_context);
    if (seat && m_context.IsPressed(seat->exitAction)) {
        m_context.Consume(seat->exitAction);
        seat->Exit();
    }
}

void Vehicle::OnDisable() {
    m_context.Deactivate();
}

void Vehicle::Serialize(Spot::Archive& archive) {
    Pawn::Serialize(archive);
    archive.Field("controls", controls, {.tooltip = "The actions a player drives it with"});
}

} // namespace Gameplay
