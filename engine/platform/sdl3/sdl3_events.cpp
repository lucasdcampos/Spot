#include "platform/platform.h"

#include "core/paths.h"
#include "platform/sdl3/sdl3_mouse_capture.h"

#include <SDL3/SDL.h>

#include <optional>

namespace Spot::Platform {

namespace {

Key ToKey(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_A:
        return Key::A;
    case SDL_SCANCODE_B:
        return Key::B;
    case SDL_SCANCODE_C:
        return Key::C;
    case SDL_SCANCODE_D:
        return Key::D;
    case SDL_SCANCODE_E:
        return Key::E;
    case SDL_SCANCODE_F:
        return Key::F;
    case SDL_SCANCODE_G:
        return Key::G;
    case SDL_SCANCODE_H:
        return Key::H;
    case SDL_SCANCODE_I:
        return Key::I;
    case SDL_SCANCODE_J:
        return Key::J;
    case SDL_SCANCODE_K:
        return Key::K;
    case SDL_SCANCODE_L:
        return Key::L;
    case SDL_SCANCODE_M:
        return Key::M;
    case SDL_SCANCODE_N:
        return Key::N;
    case SDL_SCANCODE_O:
        return Key::O;
    case SDL_SCANCODE_P:
        return Key::P;
    case SDL_SCANCODE_Q:
        return Key::Q;
    case SDL_SCANCODE_R:
        return Key::R;
    case SDL_SCANCODE_S:
        return Key::S;
    case SDL_SCANCODE_T:
        return Key::T;
    case SDL_SCANCODE_U:
        return Key::U;
    case SDL_SCANCODE_V:
        return Key::V;
    case SDL_SCANCODE_W:
        return Key::W;
    case SDL_SCANCODE_X:
        return Key::X;
    case SDL_SCANCODE_Y:
        return Key::Y;
    case SDL_SCANCODE_Z:
        return Key::Z;
    case SDL_SCANCODE_0:
        return Key::Digit0;
    case SDL_SCANCODE_1:
        return Key::Digit1;
    case SDL_SCANCODE_2:
        return Key::Digit2;
    case SDL_SCANCODE_3:
        return Key::Digit3;
    case SDL_SCANCODE_4:
        return Key::Digit4;
    case SDL_SCANCODE_5:
        return Key::Digit5;
    case SDL_SCANCODE_6:
        return Key::Digit6;
    case SDL_SCANCODE_7:
        return Key::Digit7;
    case SDL_SCANCODE_8:
        return Key::Digit8;
    case SDL_SCANCODE_9:
        return Key::Digit9;
    case SDL_SCANCODE_ESCAPE:
        return Key::Escape;
    case SDL_SCANCODE_RETURN:
        return Key::Enter;
    case SDL_SCANCODE_TAB:
        return Key::Tab;
    case SDL_SCANCODE_BACKSPACE:
        return Key::Backspace;
    case SDL_SCANCODE_SPACE:
        return Key::Space;
    case SDL_SCANCODE_MINUS:
        return Key::Minus;
    case SDL_SCANCODE_EQUALS:
        return Key::Equals;
    case SDL_SCANCODE_LEFTBRACKET:
        return Key::LeftBracket;
    case SDL_SCANCODE_RIGHTBRACKET:
        return Key::RightBracket;
    // ISO keyboards report the key left of Enter as either code depending on the OS; it is the same key.
    case SDL_SCANCODE_BACKSLASH:
    case SDL_SCANCODE_NONUSHASH:
        return Key::Backslash;
    case SDL_SCANCODE_SEMICOLON:
        return Key::Semicolon;
    case SDL_SCANCODE_APOSTROPHE:
        return Key::Apostrophe;
    case SDL_SCANCODE_GRAVE:
        return Key::Grave;
    case SDL_SCANCODE_COMMA:
        return Key::Comma;
    case SDL_SCANCODE_PERIOD:
        return Key::Period;
    case SDL_SCANCODE_SLASH:
        return Key::Slash;
    case SDL_SCANCODE_NONUSBACKSLASH:
        return Key::IntlBackslash;
    case SDL_SCANCODE_INTERNATIONAL1:
        return Key::IntlRo;
    case SDL_SCANCODE_CAPSLOCK:
        return Key::CapsLock;
    case SDL_SCANCODE_SCROLLLOCK:
        return Key::ScrollLock;
    case SDL_SCANCODE_NUMLOCKCLEAR:
        return Key::NumLock;
    case SDL_SCANCODE_PRINTSCREEN:
        return Key::PrintScreen;
    case SDL_SCANCODE_PAUSE:
        return Key::Pause;
    case SDL_SCANCODE_APPLICATION:
        return Key::Menu;
    case SDL_SCANCODE_F1:
        return Key::F1;
    case SDL_SCANCODE_F2:
        return Key::F2;
    case SDL_SCANCODE_F3:
        return Key::F3;
    case SDL_SCANCODE_F4:
        return Key::F4;
    case SDL_SCANCODE_F5:
        return Key::F5;
    case SDL_SCANCODE_F6:
        return Key::F6;
    case SDL_SCANCODE_F7:
        return Key::F7;
    case SDL_SCANCODE_F8:
        return Key::F8;
    case SDL_SCANCODE_F9:
        return Key::F9;
    case SDL_SCANCODE_F10:
        return Key::F10;
    case SDL_SCANCODE_F11:
        return Key::F11;
    case SDL_SCANCODE_F12:
        return Key::F12;
    case SDL_SCANCODE_INSERT:
        return Key::Insert;
    case SDL_SCANCODE_DELETE:
        return Key::Delete;
    case SDL_SCANCODE_HOME:
        return Key::Home;
    case SDL_SCANCODE_END:
        return Key::End;
    case SDL_SCANCODE_PAGEUP:
        return Key::PageUp;
    case SDL_SCANCODE_PAGEDOWN:
        return Key::PageDown;
    case SDL_SCANCODE_LEFT:
        return Key::Left;
    case SDL_SCANCODE_RIGHT:
        return Key::Right;
    case SDL_SCANCODE_UP:
        return Key::Up;
    case SDL_SCANCODE_DOWN:
        return Key::Down;
    case SDL_SCANCODE_KP_0:
        return Key::Numpad0;
    case SDL_SCANCODE_KP_1:
        return Key::Numpad1;
    case SDL_SCANCODE_KP_2:
        return Key::Numpad2;
    case SDL_SCANCODE_KP_3:
        return Key::Numpad3;
    case SDL_SCANCODE_KP_4:
        return Key::Numpad4;
    case SDL_SCANCODE_KP_5:
        return Key::Numpad5;
    case SDL_SCANCODE_KP_6:
        return Key::Numpad6;
    case SDL_SCANCODE_KP_7:
        return Key::Numpad7;
    case SDL_SCANCODE_KP_8:
        return Key::Numpad8;
    case SDL_SCANCODE_KP_9:
        return Key::Numpad9;
    case SDL_SCANCODE_KP_PERIOD:
        return Key::NumpadDecimal;
    case SDL_SCANCODE_KP_DIVIDE:
        return Key::NumpadDivide;
    case SDL_SCANCODE_KP_MULTIPLY:
        return Key::NumpadMultiply;
    case SDL_SCANCODE_KP_MINUS:
        return Key::NumpadSubtract;
    case SDL_SCANCODE_KP_PLUS:
        return Key::NumpadAdd;
    case SDL_SCANCODE_KP_ENTER:
        return Key::NumpadEnter;
    case SDL_SCANCODE_LSHIFT:
        return Key::LeftShift;
    case SDL_SCANCODE_LCTRL:
        return Key::LeftControl;
    case SDL_SCANCODE_LALT:
        return Key::LeftAlt;
    case SDL_SCANCODE_LGUI:
        return Key::LeftSuper;
    case SDL_SCANCODE_RSHIFT:
        return Key::RightShift;
    case SDL_SCANCODE_RCTRL:
        return Key::RightControl;
    case SDL_SCANCODE_RALT:
        return Key::RightAlt;
    case SDL_SCANCODE_RGUI:
        return Key::RightSuper;
    default:
        return Key::Unknown;
    }
}

std::optional<MouseButton> ToMouseButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return std::nullopt;
    }
}

// Matches Window::GetContentScale(), for windows the event names.
float GetContentScale(SDL_Window* window) {
    if (!window) {
        return 1.0f;
    }
    const float displayScale = SDL_GetWindowDisplayScale(window);
    const float pixelDensity = SDL_GetWindowPixelDensity(window);
    return displayScale > 0.0f && pixelDensity > 0.0f ? displayScale / pixelDensity : 1.0f;
}

GamepadId ToGamepadId(SDL_JoystickID id) {
    return static_cast<GamepadId>(id);
}

// SDL's axes go from -32768 to 32767, with Y down; the engine's sticks go from -1 to 1 with Y up.
float ToAxisValue(Uint8 axis, Sint16 value) {
    const float scaled = std::max(static_cast<float>(value) / 32767.0f, -1.0f);
    const bool vertical = axis == SDL_GAMEPAD_AXIS_LEFTY || axis == SDL_GAMEPAD_AXIS_RIGHTY;
    return vertical ? -scaled : scaled;
}

std::optional<Event> Translate(const SDL_Event& event) {
    switch (event.type) {
    // The engine has a single window, so quitting the application and closing the window are the same request.
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return WindowCloseEvent{};
    case SDL_EVENT_WINDOW_RESIZED:
        return WindowResizeEvent{event.window.data1, event.window.data2};
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return WindowFocusEvent{true};
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return WindowFocusEvent{false};
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        return WindowContentScaleEvent{GetContentScale(SDL_GetWindowFromID(event.window.windowID))};
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return MouseLeaveEvent{};
    case SDL_EVENT_KEY_DOWN:
        return KeyPressedEvent{ToKey(event.key.scancode), event.key.repeat};
    case SDL_EVENT_KEY_UP:
        return KeyReleasedEvent{ToKey(event.key.scancode)};
    case SDL_EVENT_TEXT_INPUT:
        if (!event.text.text || *event.text.text == '\0') {
            return std::nullopt;
        }
        return TextInputEvent{event.text.text};
    case SDL_EVENT_TEXT_EDITING:
        return TextCompositionEvent{event.edit.text ? event.edit.text : "", event.edit.start, event.edit.length};
    case SDL_EVENT_MOUSE_MOTION: {
        Vector2 position{event.motion.x, event.motion.y};
        SDL_Window* window = SDL_GetWindowFromID(event.motion.windowID);
        if (window && SDL_GetWindowRelativeMouseMode(window)) {
            const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
            position = {SDL_GetFloatProperty(properties, CapturedMouseXProperty, position.x),
                        SDL_GetFloatProperty(properties, CapturedMouseYProperty, position.y)};
        }
        return MouseMovedEvent{position, {event.motion.xrel, event.motion.yrel}};
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (const auto button = ToMouseButton(event.button.button)) {
            return MouseButtonPressedEvent{*button};
        }
        return std::nullopt;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (const auto button = ToMouseButton(event.button.button)) {
            return MouseButtonReleasedEvent{*button};
        }
        return std::nullopt;
    case SDL_EVENT_MOUSE_WHEEL: {
        // Undo the OS "natural scrolling" inversion so positive y always means the wheel moved away from the user.
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        return MouseScrolledEvent{Vector2{event.wheel.x, event.wheel.y} * direction};
    }
    case SDL_EVENT_GAMEPAD_ADDED: {
        // Opened, so its events come in; SDL closes it again when it goes away.
        SDL_OpenGamepad(event.gdevice.which);
        const char* name = SDL_GetGamepadNameForID(event.gdevice.which);
        return GamepadConnectedEvent{ToGamepadId(event.gdevice.which), name ? name : "Gamepad"};
    }
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (SDL_Gamepad* gamepad = SDL_GetGamepadFromID(event.gdevice.which)) {
            SDL_CloseGamepad(gamepad);
        }
        return GamepadDisconnectedEvent{ToGamepadId(event.gdevice.which)};
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        if (event.gbutton.button >= static_cast<Uint8>(GamepadButton::Count)) {
            return std::nullopt;
        }
        const auto button = static_cast<GamepadButton>(event.gbutton.button);
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            return GamepadButtonPressedEvent{ToGamepadId(event.gbutton.which), button};
        }
        return GamepadButtonReleasedEvent{ToGamepadId(event.gbutton.which), button};
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (event.gaxis.axis >= static_cast<Uint8>(GamepadAxis::Count)) {
            return std::nullopt;
        }
        return GamepadAxisEvent{ToGamepadId(event.gaxis.which), static_cast<GamepadAxis>(event.gaxis.axis),
                                ToAxisValue(event.gaxis.axis, event.gaxis.value)};
    case SDL_EVENT_DROP_FILE:
        if (!event.drop.data) {
            return std::nullopt;
        }
        return FileDropEvent{Utf8ToPath(event.drop.data), {event.drop.x, event.drop.y}};
    default:
        return std::nullopt;
    }
}

} // namespace

void PollEvents(const EventCallback& callback) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (const auto translated = Translate(event)) {
            callback(*translated);
        }
    }
}

} // namespace Spot::Platform
