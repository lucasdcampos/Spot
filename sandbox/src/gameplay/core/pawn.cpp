#include "gameplay/core/pawn.h"

#include "gameplay/core/controller.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(Pawn);

Pawn* Pawn::Of(const Spot::Entity& entity) {
    return entity.GetComponentInParent<Pawn>();
}

Controller* Pawn::GetControllerOf(const Spot::Entity& entity) {
    const Pawn* pawn = Of(entity);
    return pawn ? pawn->GetController() : nullptr;
}

Controller* Pawn::GetController() const {
    const Spot::Entity* entity = GetScene().GetEntity(m_controller);
    return entity ? entity->GetComponent<Controller>() : nullptr;
}

ViewPoint Pawn::GetViewPoint() const {
    const Spot::Entity* from = GetScene().GetEntity(eye);
    const Spot::Transform& transform = (from ? *from : GetEntity()).GetTransform();
    const Spot::Quaternion rotation = transform.GetWorldRotation();
    return {.position = transform.GetWorldPosition(),
            .direction = rotation * Spot::Vector3{0.0f, 0.0f, -1.0f},
            .rotation = rotation};
}

float Pawn::GetZoom() const {
    float zoom = 1.0f;
    GetEntity().ForEachComponent<Spot::Component>([&](Spot::Component& component) {
        if (const auto* part = dynamic_cast<const ViewZoom*>(&component); part && component.IsActiveAndEnabled()) {
            zoom *= part->GetViewZoom();
        }
    });
    return zoom;
}

void Pawn::Place(Spot::Vector3 position, Spot::Vector3 forward, Spot::Vector3 /*velocity*/) {
    Spot::Transform& transform = GetEntity().GetTransform();
    transform.SetWorldPosition(position);
    if (Spot::LengthSquared(Spot::Vector3{forward.x, 0.0f, forward.z}) > 1e-6f) {
        transform.SetWorldRotation(Spot::Quaternion::LookRotation(Spot::Vector3{forward.x, 0.0f, forward.z}));
    }
}

void Pawn::ResetMotion() {
    if (auto* body = GetEntity().GetComponent<Spot::Rigidbody>()) {
        body->SetLinearVelocity({});
        body->SetAngularVelocity({});
    }
}

Pawn* Pawn::GetOccupant() const {
    const Spot::Entity* entity = GetScene().GetEntity(m_occupant);
    return entity ? entity->GetComponent<Pawn>() : nullptr;
}

void Pawn::SetOccupant(Pawn* occupant) {
    m_occupant = occupant ? occupant->GetEntity().GetId() : Spot::EntityId::Invalid;
}

void Pawn::SetState(std::string_view state, bool on) {
    // A copy, as the view may be of the state erased.
    const std::string name(state);
    const auto found = std::ranges::find(m_states, name);
    if (on && found == m_states.end()) {
        m_states.push_back(name);
    } else if (!on && found != m_states.end()) {
        m_states.erase(found);
    } else {
        return;
    }
    OnStateChanged.Emit(name, on);
}

bool Pawn::HasState(std::string_view state) const {
    return std::ranges::find(m_states, state) != m_states.end();
}

bool Pawn::HasAnyState(std::span<const std::string> states) const {
    return std::ranges::any_of(states, [this](const std::string& state) { return HasState(state); });
}

void Pawn::OnDestroy() {
    if (Controller* controller = GetController()) {
        controller->Unpossess();
    }
}

void Pawn::Serialize(Spot::Archive& archive) {
    archive.Field("eye", eye, {.tooltip = "Where it looks from, along its forward; this entity when unset"});
    archive.Field("viewCamera", viewCamera, {.tooltip = "The camera shown while a player possesses it"});
}

} // namespace Gameplay
