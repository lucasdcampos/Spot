#pragma once

#include <cstdint>
#include <string_view>

namespace Spot {

// Keys are physical positions, named after the US layout: Key::W is the key in that spot on any layout,
// so movement bindings work the same on QWERTY, AZERTY or ABNT2 keyboards.
enum class Key : std::uint8_t {
    Unknown,

    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,

    Escape,
    Enter,
    Tab,
    Backspace,
    Space,

    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,
    // Extra keys of ISO and ABNT2 keyboards: next to left shift, and (ABNT2 only) next to right shift.
    IntlBackslash,
    IntlRo,

    CapsLock,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,
    Menu,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,

    Left,
    Right,
    Up,
    Down,

    Numpad0,
    Numpad1,
    Numpad2,
    Numpad3,
    Numpad4,
    Numpad5,
    Numpad6,
    Numpad7,
    Numpad8,
    Numpad9,
    NumpadDecimal,
    NumpadDivide,
    NumpadMultiply,
    NumpadSubtract,
    NumpadAdd,
    NumpadEnter,

    LeftShift,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper,
};

enum class MouseButton : std::uint8_t {
    Left,
    Middle,
    Right,
    X1,
    X2,
};

// The buttons of a gamepad, named by where they are, as on any layout: South is A on an Xbox pad and Cross on a
// PlayStation one.
enum class GamepadButton : std::uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count,
};

// The sticks go from -1 to 1, right and up positive; the triggers from 0, released, to 1, pressed all the way.
enum class GamepadAxis : std::uint8_t {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

// Tells a connected gamepad apart from the others while it stays connected.
enum class GamepadId : std::uint32_t { Invalid = 0 };

std::string_view GetKeyName(Key key);
std::string_view GetMouseButtonName(MouseButton button);
std::string_view GetGamepadButtonName(GamepadButton button);
std::string_view GetGamepadAxisName(GamepadAxis axis);

} // namespace Spot
