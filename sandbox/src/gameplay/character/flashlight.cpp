#include "gameplay/character/flashlight.h"

#include "gameplay/core/pawn.h"

namespace Gameplay {

SPOT_COMPONENT(Flashlight);

namespace {

template <typename F>
void Visit(Spot::Entity& entity, F& visit) {
    if (auto* found = entity.GetComponent<Spot::Light>()) {
        visit(*found);
    }
    entity.ForEachChild([&](Spot::Entity& child) { Visit(child, visit); });
}

} // namespace

template <typename F>
void Flashlight::ForEachLight(F&& visit) const {
    if (Spot::Entity* entity = GetScene().GetEntity(light)) {
        Visit(*entity, visit);
    }
}

void Flashlight::Toggle() {
    const bool on = !IsOn();
    ForEachLight([&](Spot::Light& found) { found.SetEnabled(on); });
}

bool Flashlight::IsOn() const {
    bool on = false;
    ForEachLight([&](const Spot::Light& found) { on = on || found.IsEnabled(); });
    return on;
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
