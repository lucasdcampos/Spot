#pragma once

#include "spot/platform/input_action.h"

#include <string_view>
#include <vector>

namespace Spot {

class Archive;

// The input section of a game's settings: the game's actions, which the application puts in its action map before
// the game starts.
struct InputSettings {
    static constexpr std::string_view SectionName = "input";

    std::vector<InputAction> actions = {};

    // The actions new projects start with: Move, Look, Jump, Fire and Pause, for keyboard and mouse and for gamepads.
    static InputSettings MakeDefault();

    void Serialize(Archive& archive);
    // Adds the actions to the map, replacing those of the same names.
    void Apply(InputActionMap& map) const;
};

} // namespace Spot
