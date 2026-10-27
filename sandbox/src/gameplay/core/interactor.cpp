#include "gameplay/core/interactor.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/core/interactable.h"
#include "gameplay/core/pawn.h"

namespace Gameplay {

SPOT_COMPONENT(Interactor);

bool Interactor::Interact() {
    auto target = FindTarget();
    if (!target) {
        return false;
    }
    if (target->interactable) {
        target->interactable->Interact(*this);
    } else {
        target->handler->Interact(*this, target->hit);
    }
    return true;
}

Pawn* Interactor::GetPawn() const {
    return Pawn::Of(GetEntity());
}

std::optional<Interactor::Target> Interactor::FindTarget() const {
    const Pawn* pawn = GetPawn();
    if (!pawn || pawn->HasAnyState(blockedBy)) {
        return std::nullopt;
    }
    const ViewPoint view = pawn->GetViewPoint();
    const auto hit = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(view.position, view.direction, reach,
                                                                             {.ignore = pawn->GetEntity().GetId()});
    Spot::Entity* entity = hit ? GetScene().GetEntity(hit->entity) : nullptr;
    if (!entity) {
        return std::nullopt;
    }
    if (auto* interactable = entity->GetComponentInParent<Interactable>();
        interactable && interactable->IsActiveAndEnabled() && interactable->CanInteract(*this)) {
        return Target{.hit = *hit, .interactable = interactable, .prompt = interactable->GetPrompt(*this)};
    }
    std::optional<Target> found;
    GetEntity().ForEachComponent<Spot::Component>([&](Spot::Component& component) {
        auto* handler = dynamic_cast<InteractionHandler*>(&component);
        if (found || !handler || !component.IsActiveAndEnabled()) {
            return;
        }
        if (auto prompt = handler->GetInteraction(*this, *hit)) {
            found = Target{.hit = *hit, .handler = handler, .prompt = std::move(*prompt)};
        }
    });
    return found;
}

void Interactor::UpdateFocus() {
    const auto target = m_input.IsAvailable(action) ? FindTarget() : std::nullopt;
    m_focus = target ? target->hit.entity : Spot::EntityId::Invalid;
    m_prompt = target ? target->prompt : std::string();
}

void Interactor::OnUpdate(float /*deltaTime*/) {
    if (!m_input.Takes(action)) {
        m_input.SetActions({action});
    }
    m_input.Activate(Pawn::GetControllerOf(GetEntity()));
    UpdateFocus();
    if (HasFocus() && m_input.IsPressed(action) && Interact()) {
        m_input.Consume(action);
    }
}

void Interactor::OnDisable() {
    m_input.Deactivate();
    m_focus = Spot::EntityId::Invalid;
    m_prompt.clear();
}

void Interactor::Serialize(Spot::Archive& archive) {
    archive.Field("reach", reach, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("action", action, {.tooltip = "The input action that uses what is in focus"});
    archive.Field("blockedBy", blockedBy, {.tooltip = "Pawn states during which it uses nothing"});
}

} // namespace Gameplay
