#include "spot/platform/input.h"
#include "spot/scene/scripting/script_module_bindings.h"

namespace Spot::Detail {
namespace {
constexpr std::array<std::string_view, 3> KeyQueries{"IsKeyDown", "IsKeyPressed", "IsKeyReleased"};
constexpr std::array<std::string_view, 3> MouseQueries{"IsMouseButtonDown", "IsMouseButtonPressed",
                                                       "IsMouseButtonReleased"};
constexpr std::array<std::string_view, 3> GamepadQueries{"IsGamepadButtonDown", "IsGamepadButtonPressed",
                                                         "IsGamepadButtonReleased"};

} // namespace

ScriptValue CallScriptInput(std::string_view name, std::span<const ScriptValue> arguments) {
    if (Contains(KeyQueries, name)) {
        Count(arguments, 1);
        const auto keyName = Argument<std::string>(arguments[0]);
        for (int i = 1; i <= static_cast<int>(Key::RightSuper); ++i) {
            const auto key = static_cast<Key>(i);
            if (GetKeyName(key) == keyName) {
                if (name == "IsKeyDown")
                    return Input::IsKeyDown(key);
                if (name == "IsKeyPressed")
                    return Input::IsKeyPressed(key);
                return Input::IsKeyReleased(key);
            }
        }
        throw std::runtime_error("Unknown key name '" + keyName + "'");
    }
    if (Contains(MouseQueries, name)) {
        Count(arguments, 1);
        const auto button =
            ParseName<MouseButton>(Argument<std::string>(arguments[0]), 0, static_cast<int>(MouseButton::X2),
                                   GetMouseButtonName, "mouse button");
        if (name == "IsMouseButtonDown")
            return Input::IsMouseButtonDown(button);
        if (name == "IsMouseButtonPressed")
            return Input::IsMouseButtonPressed(button);
        return Input::IsMouseButtonReleased(button);
    }
    // Gamepad queries take an optional zero-based gamepad index; without it they look at every gamepad.
    const auto gamepad = [&](std::size_t index) {
        if (arguments.size() < index + 1 || arguments.size() > index + 2)
            throw std::runtime_error("Incorrect argument count");
        return arguments.size() == index + 2 ? Argument<int>(arguments[index + 1]) : Input::AnyGamepad;
    };
    if (Contains(GamepadQueries, name)) {
        const auto index = gamepad(0);
        const auto button =
            ParseName<GamepadButton>(Argument<std::string>(arguments[0]), 0, static_cast<int>(GamepadButton::Count) - 1,
                                     GetGamepadButtonName, "gamepad button");
        if (name == "IsGamepadButtonDown")
            return Input::IsGamepadButtonDown(button, index);
        if (name == "IsGamepadButtonPressed")
            return Input::IsGamepadButtonPressed(button, index);
        return Input::IsGamepadButtonReleased(button, index);
    }
    if (name == "GetGamepadAxis") {
        const auto index = gamepad(0);
        const auto axis =
            ParseName<GamepadAxis>(Argument<std::string>(arguments[0]), 0, static_cast<int>(GamepadAxis::Count) - 1,
                                   GetGamepadAxisName, "gamepad axis");
        return static_cast<double>(Input::GetGamepadAxis(axis, index));
    }
    Count(arguments, 1);
    const auto action = Argument<std::string>(arguments[0]);
    if (name == "IsActionDown")
        return Input::IsActionDown(action);
    if (name == "IsActionPressed")
        return Input::IsActionPressed(action);
    if (name == "IsActionReleased")
        return Input::IsActionReleased(action);
    if (name == "GetActionValue")
        return static_cast<double>(Input::GetActionValue(action));
    return Input::GetActionVector2(action);
}

ScriptValue GetScriptInput(SceneProperty property, std::string_view name) {

    if (property == SceneProperty::MousePosition)
        return Input::GetMousePosition();
    if (property == SceneProperty::MouseDelta)
        return Input::GetMouseDelta();
    if (property == SceneProperty::ScrollDelta)
        return Input::GetScrollDelta();
    if (property == SceneProperty::GamepadCount)
        return static_cast<double>(Input::GetGamepadCount());
    if (property == SceneProperty::CursorLocked)
        return Input::IsCursorLocked();
    return ScriptObject().Get(name);
}
void SetScriptCursorLocked(const ScriptValue& value) {
    Input::SetCursorLocked(Argument<bool>(value));
}

} // namespace Spot::Detail
