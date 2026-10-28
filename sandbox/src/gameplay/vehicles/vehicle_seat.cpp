#include "gameplay/vehicles/vehicle_seat.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/core/controller.h"
#include "gameplay/core/interactor.h"
#include "gameplay/core/pawn.h"
#include "gameplay/vehicles/vehicle.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Gameplay {

SPOT_COMPONENT(VehicleSeat);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

} // namespace

Vehicle* VehicleSeat::GetVehicle() const {
    return GetEntity().GetComponentInParent<Vehicle>();
}

Pawn* VehicleSeat::GetOccupant() const {
    const Spot::Entity* entity = GetScene().GetEntity(m_occupant);
    return entity ? entity->GetComponent<Pawn>() : nullptr;
}

Spot::Vector3 VehicleSeat::GetSeatPosition() const {
    const Spot::Entity* entity = GetScene().GetEntity(seat);
    return (entity ? *entity : GetEntity()).GetTransform().GetWorldPosition();
}

Spot::Quaternion VehicleSeat::GetSeatRotation() const {
    const Spot::Entity* entity = GetScene().GetEntity(seat);
    return (entity ? *entity : GetEntity()).GetTransform().GetWorldRotation();
}

void VehicleSeat::PlaceOccupant(Pawn& occupant) const {
    occupant.GetEntity().GetTransform().SetWorldPosition(GetSeatPosition());
    occupant.GetEntity().GetTransform().SetWorldRotation(GetSeatRotation());
}

bool VehicleSeat::CanInteract(const Interactor& /*interactor*/) const {
    const Vehicle* vehicle = GetVehicle();
    return vehicle && vehicle->IsActiveAndEnabled() && vehicle->IsOperational() && !IsOccupied() &&
           !vehicle->HasDriver();
}

void VehicleSeat::Interact(Interactor& interactor) {
    if (Pawn* pawn = interactor.GetPawn()) {
        Enter(*pawn);
    }
}

bool VehicleSeat::Enter(Pawn& occupant) {
    Vehicle* vehicle = GetVehicle();
    Controller* controller = occupant.GetController();
    if (!IsActiveAndEnabled() || !vehicle || !vehicle->IsActiveAndEnabled() || !vehicle->IsOperational() ||
        !controller || IsOccupied() || vehicle->HasDriver() || &occupant == vehicle || occupant.HasState("Seated") ||
        occupant.HasAnyState(ejectWhen)) {
        return false;
    }
    m_occupant = occupant.GetEntity().GetId();
    occupant.SetState("Seated", true);
    if (!poseState.empty()) {
        occupant.SetState(poseState, true);
    }
    PlaceOccupant(occupant);
    vehicle->SetOccupant(&occupant);
    // What gets the driver out at once, as dying.
    m_states = occupant.OnStateChanged.Connect([this](std::string_view state, bool on) {
        if (on && std::ranges::find(ejectWhen, state) != ejectWhen.end()) {
            Exit();
        }
    });
    if (auto* body = vehicle->GetEntity().GetComponent<Spot::Rigidbody>()) {
        body->WakeUp();
    }
    controller->Possess(*vehicle);
    vehicle->OnEntered.Emit(occupant);
    return true;
}

void VehicleSeat::Exit() {
    const bool occupied = m_occupant != Spot::EntityId::Invalid;
    Pawn* occupant = GetOccupant();
    Vehicle* vehicle = GetVehicle();
    m_occupant = Spot::EntityId::Invalid;
    m_states.Disconnect();
    if (vehicle && vehicle->GetOccupant() == occupant) {
        vehicle->SetOccupant(nullptr);
    }
    if (!occupant) {
        if (occupied && vehicle) {
            if (Controller* controller = vehicle->GetController()) {
                controller->Unpossess();
            }
        }
        return;
    }
    Controller* controller = vehicle ? vehicle->GetController() : nullptr;
    occupant->SetState("Seated", false);
    if (!poseState.empty()) {
        occupant->SetState(poseState, false);
    }
    if (vehicle) {
        const Spot::Transform& transform = vehicle->GetEntity().GetTransform();
        const auto* body = vehicle->GetEntity().GetComponent<Spot::Rigidbody>();
        occupant->Place(FindExit(*vehicle, *occupant), transform.GetWorldRotation() * Spot::Vector3{0.0f, 0.0f, -1.0f},
                        body ? body->GetLinearVelocity() : Spot::Vector3{});
    }
    if (controller) {
        controller->Possess(*occupant);
    }
    if (vehicle) {
        vehicle->OnExited.Emit();
    }
}

Spot::Vector3 VehicleSeat::FindExit(const Vehicle& vehicle, const Pawn& occupant) const {
    const Spot::Transform& transform = vehicle.GetEntity().GetTransform();
    const Spot::Vector3 origin = transform.GetWorldPosition();
    const Spot::Quaternion rotation = transform.GetWorldRotation();
    const auto* controller = occupant.GetEntity().GetComponent<Spot::CharacterController>();
    const float radius = controller ? controller->GetRadius() : 0.4f;
    const float height = controller ? controller->GetHeight() : 1.8f;
    const float standing = height * 0.5f + 0.1f;

    std::vector<Spot::Vector3> spots;
    for (const Spot::EntityId id : exits) {
        if (const Spot::Entity* exit = GetScene().GetEntity(id)) {
            spots.push_back(exit->GetTransform().GetWorldPosition() + Up * standing);
        }
    }
    // Around its boxes, a hand's width from them: beside the seat first, then the other side, behind and in front.
    constexpr float Far = std::numeric_limits<float>::max();
    Spot::Vector3 low{Far, Far, Far};
    Spot::Vector3 high{-Far, -Far, -Far};
    for (const Spot::BoxCollider* box : vehicle.GetEntity().GetComponents<Spot::BoxCollider>()) {
        const Spot::Vector3 half = box->GetSize() * 0.5f;
        const Spot::Vector3 center = box->GetCenter();
        low = {std::min(low.x, center.x - half.x), std::min(low.y, center.y - half.y),
               std::min(low.z, center.z - half.z)};
        high = {std::max(high.x, center.x + half.x), std::max(high.y, center.y + half.y),
                std::max(high.z, center.z + half.z)};
    }
    if (low.x <= high.x) {
        const Spot::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
        const Spot::Vector3 seatPosition = inverse * (GetSeatPosition() - origin);
        const float side = std::max(std::abs(low.x), std::abs(high.x)) + 0.25f + radius;
        for (const Spot::Vector3 spot : {Spot::Vector3{std::copysign(side, seatPosition.x), standing, seatPosition.z},
                                         Spot::Vector3{-std::copysign(side, seatPosition.x), standing, seatPosition.z},
                                         Spot::Vector3{0.0f, standing, high.z + 0.25f + radius},
                                         Spot::Vector3{0.0f, standing, low.z - 0.25f - radius}}) {
            spots.push_back(origin + rotation * spot);
        }
    }
    auto& physics = GetScene().GetOrAddSystem<Spot::PhysicsWorld>();
    const Spot::Vector3 halfway = Up * (height * 0.5f - radius);
    for (const Spot::Vector3& position : spots) {
        if (physics.OverlapSphere(position + halfway, radius * 0.95f).empty() &&
            physics.OverlapSphere(position - halfway, radius * 0.95f).empty()) {
            return position;
        }
    }
    return origin + Up * (2.0f + height);
}

void VehicleSeat::OnLateUpdate(float /*deltaTime*/) {
    if (Pawn* occupant = GetOccupant()) {
        PlaceOccupant(*occupant);
    } else if (m_occupant != Spot::EntityId::Invalid) {
        Exit();
    }
}

void VehicleSeat::OnDisable() {
    Exit();
}

void VehicleSeat::Serialize(Spot::Archive& archive) {
    Interactable::Serialize(archive);
    archive.Field("seat", seat, {.tooltip = "Where the driver sits; this entity when unset"});
    archive.Field("exits", exits, {.tooltip = "Where the driver gets out, on the ground, the first tried first"});
    archive.Field("exitAction", exitAction);
    archive.Field("ejectWhen", ejectWhen, {.tooltip = "States of the driver that get it out at once"});
    archive.Field("poseState", poseState, {.tooltip = "Optional occupant state for the seat's presentation pose"});
}

} // namespace Gameplay
