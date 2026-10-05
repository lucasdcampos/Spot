#pragma once

#include "input/keys.h"

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

struct KeyPressedEvent {
    Key key;
    bool repeat;
};

struct KeyReleasedEvent {
    Key key;
};

// The delta is reported separately because the position stops changing while the cursor is locked.
struct MouseMovedEvent {
    float x;
    float y;
    float deltaX;
    float deltaY;
};

struct MouseButtonPressedEvent {
    MouseButton button;
};

struct MouseButtonReleasedEvent {
    MouseButton button;
};

struct MouseScrolledEvent {
    float deltaX;
    float deltaY;
};

class Event {
public:
    using Variant =
        std::variant<WindowCloseEvent, WindowResizeEvent, WindowFocusEvent, KeyPressedEvent, KeyReleasedEvent,
                     MouseMovedEvent, MouseButtonPressedEvent, MouseButtonReleasedEvent, MouseScrolledEvent>;

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
