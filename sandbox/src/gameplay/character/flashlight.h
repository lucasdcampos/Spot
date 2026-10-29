#pragma once

#include "gameplay/core/input_context.h"

#include <spot.h>

#include <string>

namespace Gameplay {

// Switches the light its pawn carries, with the action, read through the pawn's controller: the Light on the light
// entity and those below it, as a vehicle's two headlights.
class Flashlight : public Spot::Component {
public:
    // The entity of the Light it switches, as one turning with the head.
    Spot::EntityId light = Spot::EntityId::Invalid;
    std::string action = "Flashlight";

    void Toggle();
    bool IsOn() const;

protected:
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    template <typename F>
    void ForEachLight(F&& visit) const;

    InputContext m_input{{.name = "Flashlight", .actions = {"Flashlight"}}};
};

} // namespace Gameplay
