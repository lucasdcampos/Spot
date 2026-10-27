#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_input.h"
#include "gameplay/hud/input_prompts.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class VehicleSeat;

// How the driver works a vehicle. Drive goes from -1 to 1: forward accelerates and back brakes, and once stopped, back
// reverses and forward brakes, as with an automatic gearbox. Steer goes from -1, full left, to 1. A new kind of
// vehicle adds what else it needs.
struct VehicleInput {
    float drive = 0.0f;
    float steer = 0.0f;
    bool handbrake = false;
    // Vertical intent for aircraft, from -1 down to 1 up.
    float lift = 0.0f;
    // Lateral intent, from -1 left to 1 right; aircraft bank in this direction.
    float strafe = 0.0f;
};

// The actions a player's controls give a vehicle its input from: drive is accelerate less brake, steer is the X of the
// steer action.
struct VehicleControls {
    std::string accelerate = "Accelerate";
    std::string brake = "Brake";
    std::string steer = "Move";
    std::string handbrake = "Jump";
    // Optional actions for vehicles that move vertically.
    std::string ascend;
    std::string descend;
    std::string strafe;

    std::vector<std::string> GetActions() const;
    VehicleInput Read(const InputContext& input) const;
    void Serialize(Spot::Archive& archive);
};

// One reading a vehicle shows its driver, such as {"Speed", "54", "km/h"}.
struct Gauge {
    std::string label;
    std::string value;
    std::string unit;
};

// A pawn that is driven: its driver possesses it from its VehicleSeat, and gives it its input, a player's from the
// controls, anyone else with SetInput(). While possessed it is "InVehicle", for whatever shows what fits. Each kind of
// vehicle implements its physics from the input, its forward speed and the gauges it shows; entering, exiting, the
// camera, the HUD, damage and saving serve every kind without knowing it.
class Vehicle : public Pawn, public PlayerInputListener {
public:
    VehicleControls controls;

    // What it does when no player drives it; a player's controls replace it each frame.
    void SetInput(const VehicleInput& input) { m_input = IsOperational() ? input : VehicleInput{}; }
    const VehicleInput& GetInput() const { return m_input; }
    bool IsOperational() const { return !HasState("Dead") && !HasState("Destroyed"); }
    // Whether someone drives it, possessing it.
    bool HasDriver() const { return IsPossessed(); }
    // The seat its driver sits in; null when it has none.
    VehicleSeat* GetSeat() const;

    // Meters per second forward, negative backing up.
    virtual float GetForwardSpeed() const = 0;
    // What its driver sees of it, the first gauge the largest.
    virtual std::vector<Gauge> DescribeTelemetry() const;
    // Its controls, as the HUD shows them.
    virtual std::vector<InputHint> DescribeHints() const;
    // For what sounds like it: the engine's revolutions a minute, at idle and now, from 0 to 1 how open its throttle
    // is, and how far past their peak grip its tires slide, 1 at their peak.
    virtual float GetIdleRpm() const { return 0.0f; }
    virtual float GetRpm() const { return 0.0f; }
    virtual float GetThrottle() const { return 0.0f; }
    virtual float GetSlip() const { return 0.0f; }

    // The pawn that got in, and as it got out.
    Spot::Signal<Pawn&> OnEntered;
    Spot::Signal<> OnExited;

protected:
    void OnPossess(Controller& controller) override;
    void OnUnpossess() override;
    bool IsInputBeforeFixedSteps() const override { return true; }
    void OnPlayerInput(PlayerController& player) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    InputContext m_context{{.name = "Drive", .priority = 50}};
    VehicleInput m_input;
};

} // namespace Gameplay
