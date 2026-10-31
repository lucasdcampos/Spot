#include "gameplay/character/flashlight.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"

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
    SetOn(!IsOn());
}

void Flashlight::SetOn(bool on) {
    ForEachLight([&](Spot::Light& found) { found.SetEnabled(on); });
}

bool Flashlight::IsOn() const {
    bool on = false;
    ForEachLight([&](const Spot::Light& found) { on = on || found.IsEnabled(); });
    return on;
}

void Flashlight::OnPlayerInput(PlayerController& player) {
    if (!m_input.Takes(action)) {
        m_input.SetActions({action});
    }
    m_input.Activate(&player);
    if (m_input.IsPressed(action)) {
        Toggle();
    }
}

void Flashlight::SerializeIntent(Spot::Archive& archive) {
    bool on = IsOn();
    archive.Field("on", on);
    if (archive.IsLoading()) {
        SetOn(on);
    }
}

void Flashlight::Replicate(Spot::Archive& archive) {
    bool on = IsOn();
    archive.Field("on", on);
    // Its own player's machine switches it as the player does.
    const Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
    if (archive.IsLoading() && !(object && object->IsOwnedLocally())) {
        SetOn(on);
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
