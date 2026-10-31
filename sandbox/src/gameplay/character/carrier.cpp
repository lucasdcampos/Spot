#include "gameplay/character/carrier.h"

#include "gameplay/core/body_authority.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/physics_drive.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(Carrier);

bool Carrier::IsCarryable(const Spot::Entity& body, float distance) const {
    const auto* rigidbody = body.GetComponent<Spot::Rigidbody>();
    // Kinematic where another machine simulates it, which decides.
    return distance <= reach && rigidbody && (!rigidbody->IsKinematic() || !Spot::Network::HasAuthority(body)) &&
           rigidbody->GetMass() <= maxMass;
}

std::optional<std::string> Carrier::GetInteraction(const Interactor& /*interactor*/,
                                                   const Spot::RaycastHit& hit) const {
    const Spot::Entity* body = GetScene().GetEntity(hit.entity);
    if (IsHolding() || !body || !IsCarryable(*body, hit.distance)) {
        return std::nullopt;
    }
    return "Pick up";
}

void Carrier::Interact(Interactor& /*interactor*/, const Spot::RaycastHit& hit) {
    if (Spot::Entity* body = GetScene().GetEntity(hit.entity)) {
        PickUp(*body);
    }
}

void Carrier::SetHandsBusy(bool busy) {
    if (auto* pawn = Pawn::Of(GetEntity())) {
        pawn->SetState("HandsBusy", busy);
    }
}

void Carrier::PickUp(Spot::Entity& body) {
    const Pawn* pawn = Pawn::Of(GetEntity());
    // A player on another machine carries it there, at once, rather than as the server last heard.
    if (const Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
        object && object->GetOwner() != Spot::NetPeerId::Invalid) {
        BodyAuthority::Give(body, object->GetOwner());
    }
    m_held = body.GetId();
    const Spot::Quaternion facing =
        PhysicsDrive::GetFacing(pawn ? PhysicsDrive::GetYaw(pawn->GetViewPoint().direction) : 0.0f);
    m_rotation = Spot::Quaternion{-facing.x, -facing.y, -facing.z, facing.w} * body.GetTransform().GetWorldRotation();
    m_carry.Activate(Pawn::GetControllerOf(GetEntity()));
    SetHandsBusy(true);
}

void Carrier::Drop() {
    if (IsHolding()) {
        SetHandsBusy(false);
    }
    m_held = Spot::EntityId::Invalid;
    m_carry.Deactivate();
    m_turn.Deactivate();
}

void Carrier::Throw() {
    // Thrown from here, where the throw is decided.
    if (Spot::Entity* held = GetScene().GetEntity(m_held)) {
        BodyAuthority::TakeBack(*held);
    }
    if (Spot::Rigidbody* body = GetHeldBody()) {
        const Pawn* pawn = Pawn::Of(GetEntity());
        body->SetLinearVelocity(pawn ? pawn->GetViewPoint().direction * throwSpeed : Spot::Vector3{});
    }
    Drop();
}

std::vector<InputHint> Carrier::GetHints() const {
    return {{throwAction, "Throw"}, {useAction, "Drop"}, {turnAction, "Rotate"}};
}

void Carrier::OnStart() {
    m_carry.SetActions({throwAction, turnAction, useAction});
    m_turn.SetActions({lookAction});
    if (auto* pawn = Pawn::Of(GetEntity())) {
        m_states = pawn->OnStateChanged.Connect([this](std::string_view state, bool on) {
            if (on && std::ranges::find(releaseWhen, state) != releaseWhen.end() &&
                Spot::Network::HasAuthority(GetEntity())) {
                Drop();
            }
        });
    }
}

void Carrier::OnUpdate(float /*deltaTime*/) {
    const Pawn* pawn = Pawn::Of(GetEntity());
    if (IsHolding() && Spot::Network::HasAuthority(GetEntity()) &&
        (!GetHeldBody() || !pawn || pawn->HasAnyState(releaseWhen))) {
        Drop();
    }
}

void Carrier::OnPlayerInput(PlayerController& player) {
    if (!IsHolding()) {
        m_carry.Deactivate();
        m_turn.Deactivate();
        return;
    }
    if (const std::vector<std::string> actions = {throwAction, turnAction, useAction};
        m_carry.GetDesc().actions != actions) {
        m_carry.SetActions(actions);
    }
    if (!m_turn.Takes(lookAction)) {
        m_turn.SetActions({lookAction});
    }
    m_carry.Activate(&player);
    CarryIntent intent{.drop = m_carry.IsPressed(useAction), .toss = m_carry.IsPressed(throwAction)};
    if (m_carry.IsDown(turnAction)) {
        m_turn.Activate(&player);
        intent.turn = m_turn.GetVector2(lookAction) * m_lookSensitivity;
    } else {
        m_turn.Deactivate();
    }
    SetIntent(intent);
}

void Carrier::SetIntent(const CarryIntent& intent) {
    if (!IsHolding() || !Spot::Network::HasAuthority(GetEntity())) {
        return;
    }
    if (intent.drop) {
        Drop();
    } else if (intent.toss) {
        Throw();
    } else if (Spot::LengthSquared(intent.turn) > 0.0f) {
        m_rotation = PhysicsDrive::TurnByLook(m_rotation, intent.turn, 1.0f);
    }
}

void Carrier::SerializeIntent(Spot::Archive& archive) {
    CarryIntent intent{.drop = m_carry.IsPressed(useAction) && IsHolding(),
                       .toss = m_carry.IsPressed(throwAction) && IsHolding()};
    if (IsHolding() && m_carry.IsDown(turnAction)) {
        intent.turn = m_turn.GetVector2(lookAction) * m_lookSensitivity;
    }
    archive.Field("drop", intent.drop);
    archive.Field("throw", intent.toss);
    archive.Field("turn", intent.turn);
    if (archive.IsLoading()) {
        SetIntent(intent);
    }
}

void Carrier::Replicate(Spot::Archive& archive) {
    Spot::EntityId held = m_held;
    archive.Field("held", held);
    if (archive.IsLoading() && held != m_held) {
        m_held = held;
        if (!IsHolding()) {
            m_carry.Deactivate();
            m_turn.Deactivate();
        }
    }
}

void Carrier::OnFixedUpdate(float fixedDeltaTime) {
    Spot::Rigidbody* body = GetHeldBody();
    const Pawn* pawn = Pawn::Of(GetEntity());
    // Where the body is simulated: the server, or the carrying player's machine it was given to.
    if (!body || !pawn || !Spot::Network::HasAuthority(body->GetEntity())) {
        return;
    }
    const ViewPoint view = pawn->GetViewPoint();
    const Spot::Vector3 position = body->GetEntity().GetTransform().GetWorldPosition();
    const Spot::Vector3 target = view.position + view.direction * holdDistance;
    // Something in the way pulled it too far.
    if (Spot::Distance(position, target) > reach * 2.0f) {
        Drop();
        return;
    }
    const Spot::Quaternion rotation = PhysicsDrive::GetFacing(PhysicsDrive::GetYaw(view.direction)) * m_rotation;
    PhysicsDrive::Toward(*body, position, target, rotation, fixedDeltaTime, 15.0f);
}

void Carrier::Serialize(Spot::Archive& archive) {
    archive.Field("reach", reach, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("maxMass", maxMass, {.min = 0.0f, .max = 10000.0f, .speed = 0.5f, .tooltip = "In kilograms"});
    archive.Field("holdDistance", holdDistance, {.min = 0.0f, .max = 10.0f, .speed = 0.05f});
    archive.Field("throwSpeed", throwSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.1f});
    archive.Field("useAction", useAction, {.tooltip = "Drops what is carried"});
    archive.Field("throwAction", throwAction);
    archive.Field("turnAction", turnAction, {.tooltip = "Held, the look turns what is carried"});
    archive.Field("lookAction", lookAction);
    archive.Field("releaseWhen", releaseWhen);
}

Spot::Rigidbody* Carrier::GetHeldBody() const {
    Spot::Entity* body = GetScene().GetEntity(m_held);
    auto* rigidbody = body ? body->GetComponent<Spot::Rigidbody>() : nullptr;
    // Kinematic too where another machine simulates it.
    return rigidbody && rigidbody->IsActiveAndEnabled() &&
                   (!rigidbody->IsKinematic() || !Spot::Network::HasAuthority(*body))
               ? rigidbody
               : nullptr;
}

} // namespace Gameplay
