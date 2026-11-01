#pragma once

#include "gameplay/core/interactable.h"
#include "gameplay/persistence/world_save.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class Pawn;
class Vehicle;

// A driver's or passenger's seat on a vehicle or below it. Interaction chooses the driver first, then the next free
// seat in hierarchy order. Its occupant is "Seated" and follows the seat's transform. Only the driver transfers
// possession to the vehicle; passengers keep their own controller and view. The exit action gets it out, as do states
// of ejectWhen, as dying: beside the vehicle, at the first of the exits with room, then wherever around the vehicle's
// colliders there is room, then above it, facing the way the vehicle does and moving as it moved; its controller
// possesses it again. In a session, only the server enters and exits seats; each seat replicates its own occupant,
// and clients request exits through Pawn::Request().
class VehicleSeat : public Interactable, public SaveListener, public Spot::Replicated {
public:
    bool driver = true;
    // Where the occupant sits; the seat's own entity when unset.
    Spot::EntityId seat = Spot::EntityId::Invalid;
    // Entities below the vehicle, on the ground where the occupant gets out, the first tried first.
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
    // Gets the pawn into this seat; passengers need VehiclePassenger on their pawn.
    bool Enter(Pawn& occupant);
    void Exit();
    // Where the occupant sits, in the world.
    Spot::Vector3 GetSeatPosition() const;
    Spot::Quaternion GetSeatRotation() const;

    bool CanInteract(const Interactor& interactor) const override;
    std::string GetPrompt(const Interactor& interactor) const override;
    void Interact(Interactor& interactor) override;
    void Replicate(Spot::Archive& archive) override;

protected:
    void OnStart() override;
    void OnBeforeLoad() override { Exit(); }
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    friend class VehicleSeatSystem;
    // Where around the vehicle the occupant fits, standing on the ground.
    Spot::Vector3 FindExit(const Vehicle& vehicle, const Pawn& occupant) const;
    void PlaceOccupant(Pawn& occupant) const;

    Spot::EntityId m_occupant = Spot::EntityId::Invalid;
    Spot::SignalConnection m_states;
};

} // namespace Gameplay
