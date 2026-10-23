#pragma once

#include "spot/core/vector.h"
#include "spot/platform/keys.h"

#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace Spot {

struct WindowCloseEvent {};

// Sizes are in screen coordinates, which differ from pixels on high-DPI displays.
struct WindowResizeEvent {
    int width;
    int height;
};

struct WindowFocusEvent {
    bool focused;
};

// The window moved to a display with another scale, or the display's scale changed; see Window::GetContentScale().
struct WindowContentScaleEvent {
    float scale;
};

struct KeyPressedEvent {
    Key key;
    bool repeat;
};

struct KeyReleasedEvent {
    Key key;
};

// Text typed while Window::StartTextInput() is on, as UTF-8, composed by the keyboard layout and input method; key
// events cannot tell which characters a key types on another layout.
struct TextInputEvent {
    std::string text;
};

// The text an input method is composing, before it is typed, as text fields show it where the cursor is: UTF-8, with
// the part being edited from start, in characters, for length characters. Empty when the composition ended.
struct TextCompositionEvent {
    std::string text;
    int start;
    int length;
};

// The delta is reported separately because the position stops changing while the cursor is locked.
struct MouseMovedEvent {
    Vector2 position;
    Vector2 delta;
};

struct MouseButtonPressedEvent {
    MouseButton button;
};

struct MouseButtonReleasedEvent {
    MouseButton button;
};

struct MouseScrolledEvent {
    Vector2 delta;
};

// A gamepad was plugged in, or was already when the application started.
struct GamepadConnectedEvent {
    GamepadId gamepad;
    std::string name;
};

struct GamepadDisconnectedEvent {
    GamepadId gamepad;
};

struct GamepadButtonPressedEvent {
    GamepadId gamepad;
    GamepadButton button;
};

struct GamepadButtonReleasedEvent {
    GamepadId gamepad;
    GamepadButton button;
};

// A stick or a trigger moved to the value; see GamepadAxis.
struct GamepadAxisEvent {
    GamepadId gamepad;
    GamepadAxis axis;
    float value;
};

// The cursor left the window; its position is unknown until it moves over the window again.
struct MouseLeaveEvent {};

// A file or folder was dropped onto the window from the OS, such as from a file manager. Several dropped together
// arrive as one event each. The position is where it was dropped, in window coordinates.
struct FileDropEvent {
    std::filesystem::path path;
    Vector2 position;
};

class Event {
public:
    using Variant = std::variant<WindowCloseEvent, WindowResizeEvent, WindowFocusEvent, WindowContentScaleEvent,
                                 KeyPressedEvent, KeyReleasedEvent, TextInputEvent, TextCompositionEvent,
                                 MouseMovedEvent, MouseButtonPressedEvent, MouseButtonReleasedEvent, MouseScrolledEvent,
                                 MouseLeaveEvent, FileDropEvent, GamepadConnectedEvent, GamepadDisconnectedEvent,
                                 GamepadButtonPressedEvent, GamepadButtonReleasedEvent, GamepadAxisEvent>;

    // Implicit so any event type can be passed where an Event is expected.
    template <typename T>
        requires std::is_constructible_v<Variant, T>
    Event(T event) : m_data(std::move(event)) {}

    template <typename T>
    bool Is() const {
        return std::holds_alternative<T>(m_data);
    }

    template <typename T>
    const T* GetIf() const {
        return std::get_if<T>(&m_data);
    }

    template <typename Visitor>
    decltype(auto) Visit(Visitor&& visitor) const {
        return std::visit(std::forward<Visitor>(visitor), m_data);
    }

private:
    Variant m_data;
};

} // namespace Spot
