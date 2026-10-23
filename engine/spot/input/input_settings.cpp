#include "spot/input/input_settings.h"

#include "spot/core/archive.h"

namespace Spot {

InputSettings InputSettings::MakeDefault() {
    InputSettings settings;
    InputActionMap map;
    InputBinding leftStick = InputBinding::Of({InputSource::GamepadStick, 0});
    leftStick.deadZone = 0.2f;
    map.Add("Move", InputActionType::Vector2)
        .Bind(InputBinding::Composite(InputControl::Of(Key::W), InputControl::Of(Key::S), InputControl::Of(Key::A),
                                      InputControl::Of(Key::D)))
        .Bind(leftStick);
    // The mouse moves pixels each frame, while a stick holds a rate: pushed all the way, it turns as a mouse moving
    // ten pixels a frame would.
    InputBinding rightStick = InputBinding::Of({InputSource::GamepadStick, 1});
    rightStick.deadZone = 0.15f;
    rightStick.scale = 10.0f;
    map.Add("Look", InputActionType::Vector2).Bind(InputBinding::Of({InputSource::MouseMotion, 0})).Bind(rightStick);
    map.Add("Jump", InputActionType::Button)
        .Bind(InputControl::Of(Key::Space))
        .Bind(InputControl::Of(GamepadButton::South));
    map.Add("Fire", InputActionType::Button)
        .Bind(InputControl::Of(MouseButton::Left))
        .Bind(InputControl::Of(GamepadAxis::RightTrigger));
    map.Add("Pause", InputActionType::Button)
        .Bind(InputControl::Of(Key::Escape))
        .Bind(InputControl::Of(GamepadButton::Start));
    settings.actions.assign(map.GetActions().begin(), map.GetActions().end());
    return settings;
}

void InputSettings::Serialize(Archive& archive) {
    // Tools edit the actions with an editor of their own, which captures the controls.
    archive.Field("actions", actions, {.hidden = true});
}

void InputSettings::Apply(InputActionMap& map) const {
    for (const InputAction& action : actions) {
        map.Add(action.name, action.type) = action;
    }
}

} // namespace Spot
