#pragma once

#include <spot.h>

#include <span>
#include <string>
#include <string_view>

namespace Gameplay {

// A control to show: the action and what doing it does, such as {"Reload", "Reload"}. Without an action, it shows its
// verb alone, as "Reloading".
struct InputHint {
    std::string action;
    std::string verb;
};

// Controls as the player has them bound now, keyboard or gamepad, rather than as keys written into text, so they stay
// right as the player rebinds them.
namespace InputPrompts {

// A control's name as a key cap shows it: the mouse's buttons as LMB, RMB and MMB, number keys as their digit.
std::string GetShortName(const Spot::InputControl& control);
// The keyboard or mouse control that does the action, as its key cap shows it; the four keys of a direction, as WASD.
// "-" for an action without bindings.
std::string GetKeyName(const Spot::InputAction& action);
// The key of the action of the current action map; "?" when it has no such action.
std::string GetKeyName(std::string_view action);
// "[R]  Reload", for each hint, with gaps between them.
std::string Format(std::span<const InputHint> hints);
std::string Format(std::string_view action, std::string_view verb);

} // namespace InputPrompts

} // namespace Gameplay
