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

## The cursor lock

`Input::SetCursorLocked(true)` hides the cursor and keeps it in place, so the mouse only moves things, as first-person
views need. The application applies the lock to its window when it changes; the editor applies it only while the
game's view has focus, and takes it back with Escape.

## Not there yet

Gamepads, input actions with bindings and rebinding, and input ownership come in the roadmap's milestone M2.
