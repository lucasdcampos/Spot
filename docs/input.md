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
movement (right and up positive), the wheel, both of their axes or one (`InputAxes`: actions of one axis read X, or
Y when it is the only one, as a wheel switching weapons does), gamepad buttons, axes and whole sticks, or composites: four buttons
making a vector (no longer than 1, so diagonals are no faster) or two making an axis. Each binding has a dead zone
(values within it count as 0, and the rest is stretched to fill the range again), a scale and inversions. The
action's value is that of the binding farthest from rest; a button action is down when its value reaches its press
point (0.5 by default), so triggers and sticks can press buttons; the press point gives axes and vectors edges too.
Presses and releases last the frame they happened in, and a press and a release within one frame are both seen, in
either order.

Games define their actions in the input settings section (`InputSettings`), which the Project Settings panel edits:
each binding's control is set by clicking it and pressing the key, button or axis (`FindPressedControl()` finds it).
New projects start with Move, Look, Jump, Fire and Pause (`InputSettings::MakeDefault()`), for keyboard and mouse and
for gamepads. The mouse moves pixels each frame while sticks hold a rate, so Look's right stick is scaled to turn as
a mouse moving ten pixels a frame would.

The application puts the settings' actions in its map (`Application::GetInputActions()`), which games may add to
from code, and updates it after each frame's events;
`Input::IsActionDown()`, `IsActionPressed()`, `IsActionReleased()`, `GetActionValue()` and `GetActionVector2()` read it,
and report nothing for actions it does not have. A map can also be made and updated (`InputActionMap::Update()`) from
code alone, and `SetGamepad()` makes it read one gamepad, one map per local player.

## Rebinding

Players change bindings while the game runs: a controls menu waits for the next control pressed
(`FindPressedControl(*Input::GetState())`) and gives it to `InputActionMap::Rebind()`, for a binding's control or one
part of a composite. Each action keeps the bindings it had when `SetDefaults()` was called, which the application does
once it has applied the game's input settings; `IsRebound()` tells which actions differ, and `ResetBindings()` goes
back to the defaults, for one action or all.

```cpp
Spot::InputActionMap& actions = application.GetInputActions();
if (const auto pressed = Spot::FindPressedControl(*Spot::Input::GetState())) {
    actions.Rebind("Jump", 0, *pressed);
    application.SaveInputRebinds();
}
```

`Application::SaveInputRebinds()` writes the rebound actions only to `input_rebinds.json` in the game's user data
folder (`Application::GetUserDataDirectory()`, `ApplicationConfig::userDataDirectory` or the platform's folder for
the game), and the application loads it when it starts, so a game update's new defaults still reach the actions a
player left alone. Games played in the editor use the project's bindings.

## The cursor lock

`Input::SetCursorLocked(true)` hides the cursor and keeps it in place, so the mouse only moves things, as first-person
views need. The application applies the lock to its window when it changes; the editor applies it only while the
game's view has focus, and takes it back with Escape. The mouse delta is zero on the frame the lock changes and the
next, so the cursor's jump is never taken for a movement.

## Input ownership

Overlays over the game, such as a console, a text field or the editor around a game it plays, take the input from it
with a capture:

```cpp
const Spot::InputCaptureId capture = Spot::Input::PushCapture("Console");
// ... the console reads events while open ...
Spot::Input::PopCapture(capture);
```

While any capture is held, keys, mouse buttons, movements, the wheel, typed text and gamepad buttons and axes no longer
reach the game's input state, so its queries and actions report nothing; what was held when the first capture began
is released (with release edges), and `IsCursorLockApplied()` is false, so the cursor is free. The game's own
requests are left alone: `IsCursorLocked()` still tells what it asked for, and the lock comes back when the last
capture ends. Captures may end in any order, and `GetCaptureOwner()` names the latest. The mouse delta is zero on
each hand-over. Overlays read events, which are still delivered; games that react to events themselves check
`Input::IsCaptured()`.

The editor's play view holds a capture named "Editor" whenever it does not have focus.
