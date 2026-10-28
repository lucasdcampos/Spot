#pragma once

#include "gameplay/core/interactable.h"
#include "gameplay/persistence/world_save.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class Pawn;
class Vehicle;

// The driver's seat of a vehicle, on the vehicle's entity or one below it whose colliders the driver looks at to get
// in. Using it gets the pawn in: it is "Seated", which its own parts follow, as its body being put aside, it rides at
// the seat's entity, and its controller possesses the vehicle. The vehicle's exit action gets it out, as do its states
// of ejectWhen, as dying: beside the vehicle, at the first of the exits with room, then wherever around the vehicle's
// colliders there is room, then above it, facing the way the vehicle does and moving as it moved; its controller
// possesses it again.
class VehicleSeat : public Interactable, public SaveListener {
public:
    // Where the driver sits; the seat's own entity when unset.
    Spot::EntityId seat = Spot::EntityId::Invalid;
    // Entities below the vehicle, on the ground where the driver gets out, the first tried first.
    std::vector<Spot::EntityId> exits;
    std::string exitAction = "Use";
    std::vector<std::string> ejectWhen = {"Dead"};
    // Optional occupant state for an authored seat-specific pose.
    std::string poseState;

    VehicleSeat() { prompt = "Drive"; }

    // The vehicle of the seat, on its entity or the nearest above it; null when there is none.
    Vehicle* GetVehicle() const;
    Pawn* GetOccupant() const;
    bool IsOccupied() const { return GetOccupant() != nullptr; }
    // Gets the pawn in; false when the seat is taken, or the pawn has no controller to drive with.
    bool Enter(Pawn& occupant);
    void Exit();
    // Where the driver sits, in the world.
    Spot::Vector3 GetSeatPosition() const;
    Spot::Quaternion GetSeatRotation() const;

    bool CanInteract(const Interactor& interactor) const override;
    void Interact(Interactor& interactor) override;

protected:
    void OnBeforeLoad() override { Exit(); }
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Where around the vehicle the occupant fits, standing on the ground.
    Spot::Vector3 FindExit(const Vehicle& vehicle, const Pawn& occupant) const;
    void PlaceOccupant(Pawn& occupant) const;

    Spot::EntityId m_occupant = Spot::EntityId::Invalid;
    Spot::SignalConnection m_states;
};

} // namespace Gameplay
