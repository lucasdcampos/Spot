#include "spot/platform/keys.h"

namespace Spot {

std::string_view GetKeyName(Key key) {
    switch (key) {
    case Key::Unknown:
        return "Unknown";
    case Key::A:
        return "A";
    case Key::B:
        return "B";
    case Key::C:
        return "C";
    case Key::D:
        return "D";
    case Key::E:
        return "E";
    case Key::F:
        return "F";
    case Key::G:
        return "G";
    case Key::H:
        return "H";
    case Key::I:
        return "I";
    case Key::J:
        return "J";
    case Key::K:
        return "K";
    case Key::L:
        return "L";
    case Key::M:
        return "M";
    case Key::N:
        return "N";
    case Key::O:
        return "O";
    case Key::P:
        return "P";
    case Key::Q:
        return "Q";
    case Key::R:
        return "R";
    case Key::S:
        return "S";
    case Key::T:
        return "T";
    case Key::U:
        return "U";
    case Key::V:
        return "V";
    case Key::W:
        return "W";
    case Key::X:
        return "X";
    case Key::Y:
        return "Y";
    case Key::Z:
        return "Z";
    case Key::Digit0:
        return "Digit0";
    case Key::Digit1:
        return "Digit1";
    case Key::Digit2:
        return "Digit2";
    case Key::Digit3:
        return "Digit3";
    case Key::Digit4:
        return "Digit4";
    case Key::Digit5:
        return "Digit5";
    case Key::Digit6:
        return "Digit6";
    case Key::Digit7:
        return "Digit7";
    case Key::Digit8:
        return "Digit8";
    case Key::Digit9:
        return "Digit9";
    case Key::Escape:
        return "Escape";
    case Key::Enter:
        return "Enter";
    case Key::Tab:
        return "Tab";
    case Key::Backspace:
        return "Backspace";
    case Key::Space:
        return "Space";
    case Key::Minus:
        return "Minus";
    case Key::Equals:
        return "Equals";
    case Key::LeftBracket:
        return "LeftBracket";
    case Key::RightBracket:
        return "RightBracket";
    case Key::Backslash:
        return "Backslash";
    case Key::Semicolon:
        return "Semicolon";
    case Key::Apostrophe:
        return "Apostrophe";
    case Key::Grave:
        return "Grave";
    case Key::Comma:
        return "Comma";
    case Key::Period:
        return "Period";
    case Key::Slash:
        return "Slash";
    case Key::IntlBackslash:
        return "IntlBackslash";
    case Key::IntlRo:
        return "IntlRo";
    case Key::CapsLock:
        return "CapsLock";
    case Key::ScrollLock:
        return "ScrollLock";
    case Key::NumLock:
        return "NumLock";
    case Key::PrintScreen:
        return "PrintScreen";
    case Key::Pause:
        return "Pause";
    case Key::Menu:
        return "Menu";
    case Key::F1:
        return "F1";
    case Key::F2:
        return "F2";
    case Key::F3:
        return "F3";
    case Key::F4:
        return "F4";
    case Key::F5:
        return "F5";
    case Key::F6:
        return "F6";
    case Key::F7:
        return "F7";
    case Key::F8:
        return "F8";
    case Key::F9:
        return "F9";
    case Key::F10:
        return "F10";
    case Key::F11:
        return "F11";
    case Key::F12:
        return "F12";
    case Key::Insert:
        return "Insert";
    case Key::Delete:
        return "Delete";
    case Key::Home:
        return "Home";
    case Key::End:
        return "End";
    case Key::PageUp:
        return "PageUp";
    case Key::PageDown:
        return "PageDown";
    case Key::Left:
        return "Left";
    case Key::Right:
        return "Right";
    case Key::Up:
        return "Up";
    case Key::Down:
        return "Down";
    case Key::Numpad0:
        return "Numpad0";
    case Key::Numpad1:
        return "Numpad1";
    case Key::Numpad2:
        return "Numpad2";
    case Key::Numpad3:
        return "Numpad3";
    case Key::Numpad4:
        return "Numpad4";
    case Key::Numpad5:
        return "Numpad5";
    case Key::Numpad6:
        return "Numpad6";
    case Key::Numpad7:
        return "Numpad7";
    case Key::Numpad8:
        return "Numpad8";
    case Key::Numpad9:
        return "Numpad9";
    case Key::NumpadDecimal:
        return "NumpadDecimal";
    case Key::NumpadDivide:
        return "NumpadDivide";
    case Key::NumpadMultiply:
        return "NumpadMultiply";
    case Key::NumpadSubtract:
        return "NumpadSubtract";
    case Key::NumpadAdd:
        return "NumpadAdd";
    case Key::NumpadEnter:
        return "NumpadEnter";
    case Key::LeftShift:
        return "LeftShift";
    case Key::LeftControl:
        return "LeftControl";
    case Key::LeftAlt:
        return "LeftAlt";
    case Key::LeftSuper:
        return "LeftSuper";
    case Key::RightShift:
        return "RightShift";
    case Key::RightControl:
        return "RightControl";
    case Key::RightAlt:
        return "RightAlt";
    case Key::RightSuper:
        return "RightSuper";
    }
    return {};
}

std::string_view GetGamepadButtonName(GamepadButton button) {
    constexpr std::string_view Names[] = {"South",          "East",     "West",       "North",       "Back",
                                          "Guide",          "Start",    "Left Stick", "Right Stick", "Left Shoulder",
                                          "Right Shoulder", "D-Pad Up", "D-Pad Down", "D-Pad Left",  "D-Pad Right"};
    const auto index = static_cast<std::size_t>(button);
    return index < std::size(Names) ? Names[index] : std::string_view();
}

std::string_view GetGamepadAxisName(GamepadAxis axis) {
    constexpr std::string_view Names[] = {"Left X", "Left Y", "Right X", "Right Y", "Left Trigger", "Right Trigger"};
    const auto index = static_cast<std::size_t>(axis);
    return index < std::size(Names) ? Names[index] : std::string_view();
}

std::string_view GetMouseButtonName(MouseButton button) {
    switch (button) {
    case MouseButton::Left:
        return "Left";
    case MouseButton::Middle:
        return "Middle";
    case MouseButton::Right:
        return "Right";
    case MouseButton::X1:
        return "X1";
    case MouseButton::X2:
        return "X2";
    }
    return {};
}

} // namespace Spot
