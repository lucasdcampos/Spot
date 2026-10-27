#include "gameplay/character/carrier.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/physics_drive.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(Carrier);

bool Carrier::IsCarryable(const Spot::Entity& body, float distance) const {
    const auto* rigidbody = body.GetComponent<Spot::Rigidbody>();
    return distance <= reach && rigidbody && !rigidbody->IsKinematic() && rigidbody->GetMass() <= maxMass;
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
            if (on && std::ranges::find(releaseWhen, state) != releaseWhen.end()) {
                Drop();
            }
        });
    }
}

void Carrier::OnUpdate(float /*deltaTime*/) {
    const Pawn* pawn = Pawn::Of(GetEntity());
    if (IsHolding() && (!GetHeldBody() || !pawn || pawn->HasAnyState(releaseWhen))) {
        Drop();
    }
    if (!IsHolding()) {
        return;
    }
    if (const std::vector<std::string> actions = {throwAction, turnAction, useAction};
        m_carry.GetDesc().actions != actions) {
        m_carry.SetActions(actions);
    }
    if (!m_turn.Takes(lookAction)) {
        m_turn.SetActions({lookAction});
    }
    m_carry.Activate(Pawn::GetControllerOf(GetEntity()));
    if (m_carry.IsPressed(useAction)) {
        Drop();
        return;
    }
    if (m_carry.IsPressed(throwAction)) {
        Throw();
        return;
    }
    if (!m_carry.IsDown(turnAction)) {
        m_turn.Deactivate();
        return;
    }
    m_turn.Activate(Pawn::GetControllerOf(GetEntity()));
    m_rotation = PhysicsDrive::TurnByLook(m_rotation, m_turn.GetVector2(lookAction), m_lookSensitivity);
}

void Carrier::OnFixedUpdate(float fixedDeltaTime) {
    Spot::Rigidbody* body = GetHeldBody();
    const Pawn* pawn = Pawn::Of(GetEntity());
    if (!body || !pawn) {
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
    return rigidbody && rigidbody->IsActiveAndEnabled() && !rigidbody->IsKinematic() ? rigidbody : nullptr;
}

} // namespace Gameplay
