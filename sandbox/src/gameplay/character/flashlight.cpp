#include "gameplay/character/flashlight.h"

#include "gameplay/core/pawn.h"

namespace Gameplay {

SPOT_COMPONENT(Flashlight);

Spot::Light* Flashlight::GetLight() const {
    Spot::Entity* entity = GetScene().GetEntity(light);
    return entity ? entity->GetComponent<Spot::Light>() : nullptr;
}

void Flashlight::Toggle() {
    if (Spot::Light* found = GetLight()) {
        found->SetEnabled(!found->IsEnabled());
    }
}

bool Flashlight::IsOn() const {
    const Spot::Light* found = GetLight();
    return found && found->IsEnabled();
}

void Flashlight::OnUpdate(float /*deltaTime*/) {
    if (!m_input.Takes(action)) {
        m_input.SetActions({action});
    }
    m_input.Activate(Pawn::GetControllerOf(GetEntity()));
    if (m_input.IsPressed(action)) {
        Toggle();
    }
}

void Flashlight::OnDisable() {
    m_input.Deactivate();
}

void Flashlight::Serialize(Spot::Archive& archive) {
    archive.Field("light", light, {.tooltip = "The light the action switches"});
    archive.Field("action", action);
}

} // namespace Gameplay
