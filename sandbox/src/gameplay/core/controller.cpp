#include "gameplay/core/controller.h"

#include "gameplay/core/pawn.h"

namespace Gameplay {

void Controller::Possess(Pawn& target) {
    if (Controller* other = target.GetController(); other == this) {
        return;
    } else if (other) {
        other->Unpossess();
    }
    Unpossess();
    pawn = target.GetEntity().GetId();
    m_possessing = true;
    target.m_controller = GetEntity().GetId();
    OnPossess(target);
    target.OnPossess(*this);
    target.OnPossessed.Emit(*this);
}

void Controller::Unpossess() {
    Pawn* current = GetPawn();
    m_possessing = false;
    pawn = Spot::EntityId::Invalid;
    if (!current) {
        return;
    }
    current->m_controller = Spot::EntityId::Invalid;
    OnUnpossess(*current);
    current->OnUnpossess();
    current->OnUnpossessed.Emit();
}

Pawn* Controller::GetPawn() const {
    const Spot::Entity* entity = m_possessing ? GetScene().GetEntity(pawn) : nullptr;
    return entity ? entity->GetComponent<Pawn>() : nullptr;
}

void Controller::OnStart() {
    const Spot::Entity* entity = m_possessing ? nullptr : GetScene().GetEntity(pawn);
    if (Pawn* target = entity ? entity->GetComponent<Pawn>() : nullptr) {
        Possess(*target);
    }
}

void Controller::OnDestroy() {
    Unpossess();
}

void Controller::Serialize(Spot::Archive& archive) {
    archive.Field("pawn", pawn, {.tooltip = "The pawn it possesses as it starts"});
}

} // namespace Gameplay
