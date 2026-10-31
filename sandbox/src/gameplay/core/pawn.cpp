#include "gameplay/core/pawn.h"

#include "gameplay/core/controller.h"
#include "gameplay/core/pawn_request.h"
#include "gameplay/core/player_input.h"

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

bool Pawn::IsPossessedWhereDecided() const {
    // A client's say on what it drives is only where it goes; the server still decides who rides it.
    return Spot::Network::IsClient(GetScene()) ? m_possessedOnServer : IsPossessed();
}

void Pawn::Request(std::string_view name) {
    if (Spot::Network::IsClient(GetScene())) {
        Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
        if (object && object->IsOwnedLocally() && IsPossessed()) {
            Spot::Network::Get(GetScene())->SendToServer(*object, PawnRequest{std::string(name)});
        }
        return;
    }
    const auto dispatch = [&](const auto& self, Spot::Entity& entity) -> void {
        if (Pawn::Of(entity) != this) {
            return;
        }
        Spot::Scene& scene = entity.GetScene();
        const Spot::EntityId id = entity.GetId();
        entity.ForEachComponent<Spot::Component>([&](Spot::Component& component) {
            if (auto* listener = dynamic_cast<PlayerInputListener*>(&component);
                listener && component.IsActiveAndEnabled()) {
                component.RunGuarded("OnPlayerRequest", [&] { listener->OnPlayerRequest(name); });
            }
        });
        if (Spot::Entity* current = scene.GetEntity(id)) {
            current->ForEachChild([&](Spot::Entity& child) { self(self, child); });
        }
    };
    dispatch(dispatch, GetEntity());
}

void Pawn::Replicate(Spot::Archive& archive) {
    std::vector<std::string> states;
    for (const std::string& state : m_states) {
        if (std::ranges::find(localStates, state) == localStates.end()) {
            states.push_back(state);
        }
    }
    bool possessed = IsPossessed();
    Spot::EntityId occupant = m_occupant;
    archive.Field("states", states);
    archive.Field("possessed", possessed);
    archive.Field("occupant", occupant);
    if (!archive.IsLoading()) {
        return;
    }
    m_possessedOnServer = possessed;
    m_occupant = occupant;
    // A copy, as setting states changes them.
    const std::vector<std::string> current = m_states;
    for (const std::string& state : current) {
        if (std::ranges::find(localStates, state) == localStates.end() &&
            std::ranges::find(states, state) == states.end()) {
            SetState(state, false);
        }
    }
    for (const std::string& state : states) {
        SetState(state, true);
    }
}

void Pawn::OnDestroy() {
    if (Controller* controller = GetController()) {
        controller->Unpossess();
    }
}

void Pawn::Serialize(Spot::Archive& archive) {
    archive.Field("eye", eye, {.tooltip = "Where it looks from, along its forward; this entity when unset"});
    archive.Field("viewCamera", viewCamera, {.tooltip = "The camera shown while a player possesses it"});
    archive.Field("localStates", localStates, {.tooltip = "States each machine keeps for itself in a session"});
}

} // namespace Gameplay
