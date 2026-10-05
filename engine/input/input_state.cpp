#include "input/input_state.h"

#include <cstddef>

namespace Spot {

namespace {

std::size_t ToIndex(Key key) {
    return static_cast<std::size_t>(key);
}

std::size_t ToIndex(MouseButton button) {
    return static_cast<std::size_t>(button);
}

} // namespace

void InputState::BeginFrame() {
    m_keysPressed.reset();
    m_keysReleased.reset();
    m_buttonsPressed.reset();
    m_buttonsReleased.reset();
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_scrollDeltaX = 0.0f;
    m_scrollDeltaY = 0.0f;
}

void InputState::OnEvent(const Event& event) {
    if (const auto* pressed = event.GetIf<KeyPressedEvent>()) {
        // Repeats come from the OS while a key is held; they are not new presses.
        if (pressed->key != Key::Unknown && !pressed->repeat) {
            m_keysDown.set(ToIndex(pressed->key));
            m_keysPressed.set(ToIndex(pressed->key));
        }
    } else if (const auto* released = event.GetIf<KeyReleasedEvent>()) {
        if (released->key != Key::Unknown) {
            m_keysDown.reset(ToIndex(released->key));
            m_keysReleased.set(ToIndex(released->key));
        }
    } else if (const auto* buttonPressed = event.GetIf<MouseButtonPressedEvent>()) {
        m_buttonsDown.set(ToIndex(buttonPressed->button));
        m_buttonsPressed.set(ToIndex(buttonPressed->button));
    } else if (const auto* buttonReleased = event.GetIf<MouseButtonReleasedEvent>()) {
        m_buttonsDown.reset(ToIndex(buttonReleased->button));
        m_buttonsReleased.set(ToIndex(buttonReleased->button));
    } else if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
        m_mouseX = moved->x;
        m_mouseY = moved->y;
        m_mouseDeltaX += moved->deltaX;
        m_mouseDeltaY += moved->deltaY;
    } else if (const auto* scrolled = event.GetIf<MouseScrolledEvent>()) {
        m_scrollDeltaX += scrolled->deltaX;
        m_scrollDeltaY += scrolled->deltaY;
    } else if (const auto* focus = event.GetIf<WindowFocusEvent>(); focus && !focus->focused) {
        // Release events for keys held while focus is lost never arrive, which would leave them stuck down.
        ReleaseAll();
    }
}

bool InputState::IsKeyDown(Key key) const {
    return m_keysDown.test(ToIndex(key));
}

bool InputState::IsKeyPressed(Key key) const {
    return m_keysPressed.test(ToIndex(key));
}

bool InputState::IsKeyReleased(Key key) const {
    return m_keysReleased.test(ToIndex(key));
}

bool InputState::IsMouseButtonDown(MouseButton button) const {
    return m_buttonsDown.test(ToIndex(button));
}

bool InputState::IsMouseButtonPressed(MouseButton button) const {
    return m_buttonsPressed.test(ToIndex(button));
}

bool InputState::IsMouseButtonReleased(MouseButton button) const {
    return m_buttonsReleased.test(ToIndex(button));
}

void InputState::ReleaseAll() {
    m_keysReleased |= m_keysDown;
    m_keysDown.reset();
    m_buttonsReleased |= m_buttonsDown;
    m_buttonsDown.reset();
}

} // namespace Spot
