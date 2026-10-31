#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/core/player_input.h"

#include <spot.h>

#include <string>

namespace Gameplay {

// Switches the light its pawn carries, with the action, read through the pawn's controller: the Light on the light
// entity and those below it, as a vehicle's two headlights. In a session, the player's machine switches it at once
// and tells the server, which shows the others.
class Flashlight : public Spot::Component, public PlayerInputListener, public Spot::Replicated {
public:
    // The entity of the Light it switches, as one turning with the head.
    Spot::EntityId light = Spot::EntityId::Invalid;
    std::string action = "Flashlight";

    void Toggle();
    bool IsOn() const;
    void SetOn(bool on);

    void Replicate(Spot::Archive& archive) override;

protected:
    void OnPlayerInput(PlayerController& player) override;
    void SerializeIntent(Spot::Archive& archive) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    template <typename F>
    void ForEachLight(F&& visit) const;

    InputContext m_input{{.name = "Flashlight", .actions = {"Flashlight"}}};
};

} // namespace Gameplay
