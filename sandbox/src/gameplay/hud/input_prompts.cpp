#include "gameplay/hud/input_prompts.h"

#include <format>
#include <utility>

namespace Gameplay::InputPrompts {

std::string GetShortName(const Spot::InputControl& control) {
    const std::string name = control.GetName();
    static constexpr std::pair<std::string_view, std::string_view> Short[] = {
        {"Mouse Left", "LMB"},  {"Mouse Right", "RMB"},  {"Mouse Middle", "MMB"}, {"Mouse Wheel", "Wheel"},
        {"LeftShift", "Shift"}, {"LeftControl", "Ctrl"}, {"LeftAlt", "Alt"},      {"Escape", "Esc"}};
    for (const auto& [from, to] : Short) {
        if (name.starts_with(from)) {
            return std::string(to);
        }
    }
    return name.starts_with("Digit") ? name.substr(5) : name;
}

std::string GetKeyName(const Spot::InputAction& action) {
    for (const auto& binding : action.bindings) {
        const auto source = binding.control.source;
        if (source == Spot::InputSource::Composite && binding.parts.size() == 4 &&
            binding.parts[0].source == Spot::InputSource::Key) {
            if (action.type == Spot::InputActionType::Axis) {
                return GetShortName(binding.parts[0]) + "/" + GetShortName(binding.parts[1]);
            }
            // Stored up, down, left, right; read as the keys lie.
            return GetShortName(binding.parts[0]) + GetShortName(binding.parts[2]) + GetShortName(binding.parts[1]) +
                   GetShortName(binding.parts[3]);
        }
        if (source == Spot::InputSource::Key || source == Spot::InputSource::MouseButton ||
            source == Spot::InputSource::Scroll) {
            return GetShortName(binding.control);
        }
    }
    return action.bindings.empty() ? std::string("-") : GetShortName(action.bindings.front().control);
}

std::string GetKeyName(std::string_view action) {
    const Spot::InputActionMap* actions = Spot::Input::GetActionMap();
    const Spot::InputAction* found = actions ? actions->Find(action) : nullptr;
    return found ? GetKeyName(*found) : std::string("?");
}

std::string Format(std::string_view action, std::string_view verb) {
    return std::format("[{}]  {}", GetKeyName(action), verb);
}

std::string Format(std::span<const InputHint> hints) {
    std::string text;
    for (const InputHint& hint : hints) {
        if (!text.empty()) {
            text += "      ";
        }
        text += hint.action.empty() ? hint.verb : Format(hint.action, hint.verb);
    }
    return text;
}

} // namespace Gameplay::InputPrompts
