# Input

Input reaches games two ways: as **events**, delivered to the application and the scenes as they come, and as an
**input state** that games query each frame. The platform backend (SDL3, `engine/platform/`) turns the OS's events
into the engine's (`engine/events/event.h`) and never shows in public headers.

## Events

`Event` holds one of the engine's event types: window events (close, resize, focus, content scale, files dropped),
keyboard events (`KeyPressedEvent`, `KeyReleasedEvent`), text typed while text input is on (`TextInputEvent`), and
mouse events (moves, with a delta that keeps reporting while the cursor is locked; buttons; scrolling; leaving the
window). Each frame, the application polls them and sends each to the input state, `Application::OnEvent()` and the
scenes' `OnEvent()`.

```cpp
void OnEvent(const Spot::Event& event) override {
    if (const auto* pressed = event.GetIf<Spot::KeyPressedEvent>(); pressed && pressed->key == Spot::Key::F1) {
        // ...
    }
}
```

## The input state

`InputState` (`engine/input/input_state.h`) is the keyboard and mouse built from events. Holding a key or button is
"down"; "pressed" and "released" only hold during the frame in which they changed, so a press and a release within
one frame are both seen. The running application's state is reached anywhere through `Spot::Input`:

```cpp
if (Spot::Input::IsKeyDown(Spot::Key::W)) { /* walk */ }
if (Spot::Input::IsMouseButtonPressed(Spot::MouseButton::Left)) { /* shoot */ }
const Spot::Vector2 look = Spot::Input::GetMouseDelta();
```

Keys (`engine/input/keys.h`) name physical positions on a US layout, so WASD stays in place on other layouts; text
typed comes through `TextInputEvent` instead.

## Text

Key events tell which keys are pressed, not which characters they type on the player's layout, so text fields use
text input instead: while `Window::StartTextInput()` is on, typed text arrives as `TextInputEvent`s, and
`Input::GetTypedText()` gives what was typed this frame. Input methods for languages such as Japanese compose text
before typing it, which arrives as `TextCompositionEvent`s for fields to show at their cursor;
`Window::SetTextInputArea()` tells the input method where the field and its cursor are, so its window opens beside
them.

## Gamepads

Gamepads send their own events (`GamepadConnectedEvent`, `GamepadDisconnectedEvent`, button presses and releases, and
`GamepadAxisEvent`), and the input state tracks each one. Buttons are named by position (`GamepadButton::South` is A
on an Xbox pad and Cross on a PlayStation one); sticks go from -1 to 1 with right and up positive, and triggers from
0 to 1. Gamepads are numbered in the order they connected, so each player can have one, and queries made with
`Input::AnyGamepad` (the default) look at all of them:

```cpp
if (Spot::Input::IsGamepadButtonPressed(Spot::GamepadButton::South)) { /* jump */ }
const float steer = Spot::Input::GetGamepadAxis(Spot::GamepadAxis::LeftX, 0);
Spot::Input::RumbleGamepad(0.5f, 0.8f, 0.2f);
```

With every gamepad, an axis query gives the value farthest from rest. `RumbleGamepad()` shakes the low and high
frequency motors through the platform. Dead zones and other processing belong to input actions.

## Actions

Games rarely care which key jumps: they ask whether the player jumped. An `InputActionMap`
(`engine/input/input_action.h`) holds named actions, each with bindings to the controls that drive it:

```cpp
Spot::InputActionMap& actions = GetInputActions(); // in the Application
actions.Add("Jump", Spot::InputActionType::Button)
    .Bind(Spot::InputControl::Of(Spot::Key::Space))
    .Bind(Spot::InputControl::Of(Spot::GamepadButton::South));
actions.Add("Move", Spot::InputActionType::Vector2)
    .Bind(Spot::InputBinding::Composite(Spot::InputControl::Of(Spot::Key::W), Spot::InputControl::Of(Spot::Key::S),
                                        Spot::InputControl::Of(Spot::Key::A), Spot::InputControl::Of(Spot::Key::D)))
    .Bind({.control = {Spot::InputSource::GamepadStick, 0}, .deadZone = 0.2f});

if (Spot::Input::IsActionPressed("Jump")) { /* jump */ }
const Spot::Vector2 move = Spot::Input::GetActionVector2("Move");
```

An action is a `Button` (on or off), an `Axis` (-1 to 1) or a `Vector2`. Bindings read keys, mouse buttons, the mouse's
movement (right and up positive), the wheel, gamepad buttons, axes and whole sticks, or composites: four buttons
making a vector (no longer than 1, so diagonals are no faster) or two making an axis. Each binding has a dead zone
(values within it count as 0, and the rest is stretched to fill the range again), a scale and inversions. The
action's value is that of the binding farthest from rest; a button action is down when its value reaches its press
point (0.5 by default), so triggers and sticks can press buttons. Presses and releases last the frame they happened
in, and a press and release within one frame are both seen.

The application keeps the game's map (`Application::GetInputActions()`) and updates it after each frame's events;
`Input::IsActionDown()`, `IsActionPressed()`, `IsActionReleased()`, `GetActionValue()` and `GetActionVector2()` read it,
and report nothing for actions it does not have. A map can also be made and updated (`InputActionMap::Update()`) from
code alone, and `SetGamepad()` makes it read one gamepad, one map per local player.

## The cursor lock

`Input::SetCursorLocked(true)` hides the cursor and keeps it in place, so the mouse only moves things, as first-person
views need. The application applies the lock to its window when it changes; the editor applies it only while the
game's view has focus, and takes it back with Escape.

## Not there yet

Actions from the game's settings, rebinding, and input ownership come in the rest of the roadmap's milestone M2.
