#include "spot/input/input_state.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

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
    for (Gamepad& gamepad : m_gamepads) {
        gamepad.pressed.reset();
        gamepad.released.reset();
    }
    m_typedText.clear();
    m_mouseDelta = {};
    m_scrollDelta = {};
    m_dropMouseDeltaThisFrame = std::exchange(m_dropMouseDelta, false);
}

void InputState::OnEvent(const Event& event) {
    if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
        // Where the cursor is stays known, for when the game has the input again.
        m_mousePosition = moved->position;
        if (m_captures.empty() && !m_dropMouseDelta && !m_dropMouseDeltaThisFrame) {
            m_mouseDelta += moved->delta;
        }
        return;
    }
    const bool withheld =
        event.Is<KeyPressedEvent>() || event.Is<KeyReleasedEvent>() || event.Is<MouseButtonPressedEvent>() ||
        event.Is<MouseButtonReleasedEvent>() || event.Is<MouseScrolledEvent>() || event.Is<TextInputEvent>() ||
        event.Is<GamepadButtonPressedEvent>() || event.Is<GamepadButtonReleasedEvent>() || event.Is<GamepadAxisEvent>();
    if (withheld && !m_captures.empty()) {
        return;
    }
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
    } else if (const auto* scrolled = event.GetIf<MouseScrolledEvent>()) {
        m_scrollDelta += scrolled->delta;
    } else if (const auto* typed = event.GetIf<TextInputEvent>()) {
        m_typedText += typed->text;
    } else if (const auto* connected = event.GetIf<GamepadConnectedEvent>()) {
        FindOrAddGamepad(connected->gamepad).name = connected->name;
    } else if (const auto* disconnected = event.GetIf<GamepadDisconnectedEvent>()) {
        std::erase_if(m_gamepads, [&](const Gamepad& gamepad) { return gamepad.id == disconnected->gamepad; });
    } else if (const auto* padPressed = event.GetIf<GamepadButtonPressedEvent>()) {
        Gamepad& gamepad = FindOrAddGamepad(padPressed->gamepad);
        gamepad.down.set(static_cast<std::size_t>(padPressed->button));
        gamepad.pressed.set(static_cast<std::size_t>(padPressed->button));
    } else if (const auto* padReleased = event.GetIf<GamepadButtonReleasedEvent>()) {
        Gamepad& gamepad = FindOrAddGamepad(padReleased->gamepad);
        gamepad.down.reset(static_cast<std::size_t>(padReleased->button));
        gamepad.released.set(static_cast<std::size_t>(padReleased->button));
    } else if (const auto* axis = event.GetIf<GamepadAxisEvent>()) {
        FindOrAddGamepad(axis->gamepad).axes[static_cast<std::size_t>(axis->axis)] = axis->value;
    } else if (const auto* focus = event.GetIf<WindowFocusEvent>(); focus && !focus->focused) {
        // Release events for keys held while focus is lost never arrive, which would leave them stuck down.
        ReleaseAll();
    }
}

void InputState::ConsumeEvent(const Event& event) {
    auto key = [&](Key value) {
        const auto index = ToIndex(value);
        m_keysDown.reset(index);
        m_keysPressed.reset(index);
        m_keysReleased.reset(index);
    };
    auto button = [&](MouseButton value) {
        const auto index = ToIndex(value);
        m_buttonsDown.reset(index);
        m_buttonsPressed.reset(index);
        m_buttonsReleased.reset(index);
    };
    if (const auto* value = event.GetIf<KeyPressedEvent>())
        key(value->key);
    if (const auto* value = event.GetIf<KeyReleasedEvent>())
        key(value->key);
    if (const auto* value = event.GetIf<MouseButtonPressedEvent>())
        button(value->button);
    if (const auto* value = event.GetIf<MouseButtonReleasedEvent>())
        button(value->button);
    if (const auto* value = event.GetIf<MouseMovedEvent>()) {
        m_mousePosition = value->position;
        m_mouseDelta = {};
    }
    if (event.Is<MouseScrolledEvent>())
        m_scrollDelta = {};
    if (event.Is<TextInputEvent>())
        m_typedText.clear();
    auto padButton = [&](GamepadId id, GamepadButton value) {
        auto& pad = FindOrAddGamepad(id);
        const auto index = static_cast<std::size_t>(value);
        pad.down.reset(index);
        pad.pressed.reset(index);
        pad.released.reset(index);
    };
    if (const auto* value = event.GetIf<GamepadButtonPressedEvent>())
        padButton(value->gamepad, value->button);
    if (const auto* value = event.GetIf<GamepadButtonReleasedEvent>())
        padButton(value->gamepad, value->button);
    if (const auto* value = event.GetIf<GamepadAxisEvent>())
        FindOrAddGamepad(value->gamepad).axes[static_cast<std::size_t>(value->axis)] = 0;
}

GamepadId InputState::GetGamepadId(int gamepad) const {
    return gamepad >= 0 && gamepad < GetGamepadCount() ? m_gamepads[static_cast<std::size_t>(gamepad)].id
                                                       : GamepadId::Invalid;
}

std::string_view InputState::GetGamepadName(int gamepad) const {
    return gamepad >= 0 && gamepad < GetGamepadCount()
               ? std::string_view(m_gamepads[static_cast<std::size_t>(gamepad)].name)
               : std::string_view();
}

template <typename Test>
bool InputState::AnyGamepadMatches(int gamepad, Test&& test) const {
    if (gamepad != AnyGamepad) {
        return gamepad >= 0 && gamepad < GetGamepadCount() && test(m_gamepads[static_cast<std::size_t>(gamepad)]);
    }
    return std::ranges::any_of(m_gamepads, test);
}

bool InputState::IsGamepadButtonDown(GamepadButton button, int gamepad) const {
    return AnyGamepadMatches(gamepad,
                             [button](const Gamepad& pad) { return pad.down.test(static_cast<std::size_t>(button)); });
}

bool InputState::IsGamepadButtonPressed(GamepadButton button, int gamepad) const {
    return AnyGamepadMatches(
        gamepad, [button](const Gamepad& pad) { return pad.pressed.test(static_cast<std::size_t>(button)); });
}

bool InputState::IsGamepadButtonReleased(GamepadButton button, int gamepad) const {
    return AnyGamepadMatches(
        gamepad, [button](const Gamepad& pad) { return pad.released.test(static_cast<std::size_t>(button)); });
}

float InputState::GetGamepadAxis(GamepadAxis axis, int gamepad) const {
    const auto index = static_cast<std::size_t>(axis);
    if (gamepad != AnyGamepad) {
        return gamepad >= 0 && gamepad < GetGamepadCount() ? m_gamepads[static_cast<std::size_t>(gamepad)].axes[index]
                                                           : 0.0f;
    }
    float value = 0.0f;
    for (const Gamepad& pad : m_gamepads) {
        if (std::abs(pad.axes[index]) > std::abs(value)) {
            value = pad.axes[index];
        }
    }
    return value;
}

InputState::Gamepad& InputState::FindOrAddGamepad(GamepadId id) {
    const auto found = std::ranges::find(m_gamepads, id, &Gamepad::id);
    if (found != m_gamepads.end()) {
        return *found;
    }
    return m_gamepads.emplace_back(Gamepad{.id = id});
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

void InputState::SetCursorLocked(bool locked) {
    if (locked != m_cursorLocked) {
        m_cursorLocked = locked;
        DropMouseDelta();
    }
}

InputCaptureId InputState::PushCapture(std::string_view owner) {
    if (m_captures.empty()) {
        ReleaseAll();
        m_scrollDelta = {};
        m_typedText.clear();
        DropMouseDelta();
    }
    const auto id = static_cast<InputCaptureId>(m_nextCapture++);
    m_captures.push_back({id, std::string(owner)});
    return id;
}

void InputState::PopCapture(InputCaptureId capture) {
    const std::size_t count = m_captures.size();
    std::erase_if(m_captures, [capture](const Capture& held) { return held.id == capture; });
    if (count > 0 && m_captures.empty()) {
        DropMouseDelta();
    }
}

std::string_view InputState::GetCaptureOwner() const {
    return m_captures.empty() ? std::string_view() : std::string_view(m_captures.back().owner);
}

void InputState::DropMouseDelta() {
    m_mouseDelta = {};
    m_dropMouseDelta = true;
}

void InputState::ReleaseAll() {
    m_keysReleased |= m_keysDown;
    m_keysDown.reset();
    m_buttonsReleased |= m_buttonsDown;
    m_buttonsDown.reset();
    for (Gamepad& gamepad : m_gamepads) {
        gamepad.released |= gamepad.down;
        gamepad.down.reset();
        gamepad.axes = {};
    }
}

} // namespace Spot
