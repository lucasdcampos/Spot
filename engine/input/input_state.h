#pragma once

#include "events/event.h"
#include "input/keys.h"
#include "math/vector.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

// Keyboard, mouse and gamepad state built from events. "Pressed" and "released" only hold during the frame in which
// the change happened, so a press and release within one frame are both still seen.
//
// Gamepads are numbered in the order they connected, from 0, so a game can give each player one; queries made with
// AnyGamepad look at all of them, as single-player games do.
class InputState {
public:
    // Matches Input::AnyGamepad.
    static constexpr int AnyGamepad = -1;

    // Ends the previous frame: clears per-frame transitions and accumulated deltas.
    void BeginFrame();
    void OnEvent(const Event& event);

    bool IsKeyDown(Key key) const;
    bool IsKeyPressed(Key key) const;
    bool IsKeyReleased(Key key) const;

    bool IsMouseButtonDown(MouseButton button) const;
    bool IsMouseButtonPressed(MouseButton button) const;
    bool IsMouseButtonReleased(MouseButton button) const;

    int GetGamepadCount() const { return static_cast<int>(m_gamepads.size()); }
    // GamepadId::Invalid, or an empty name, past the last gamepad.
    GamepadId GetGamepadId(int gamepad) const;
    std::string_view GetGamepadName(int gamepad) const;
    // With AnyGamepad, whether any gamepad's button is.
    bool IsGamepadButtonDown(GamepadButton button, int gamepad = AnyGamepad) const;
    bool IsGamepadButtonPressed(GamepadButton button, int gamepad = AnyGamepad) const;
    bool IsGamepadButtonReleased(GamepadButton button, int gamepad = AnyGamepad) const;
    // With AnyGamepad, the value farthest from rest among the gamepads.
    float GetGamepadAxis(GamepadAxis axis, int gamepad = AnyGamepad) const;

    Vector2 GetMousePosition() const { return m_mousePosition; }
    Vector2 GetMouseDelta() const { return m_mouseDelta; }
    Vector2 GetScrollDelta() const { return m_scrollDelta; }

    // Whether the game asked for the cursor to be hidden and kept in place, so the mouse only moves things, as it
    // turns first-person views. The application applies it to its window; tools running a game in a view of their
    // own, such as the editor, apply it while that view has focus. Events leave it as it is.
    bool IsCursorLocked() const { return m_cursorLocked; }
    void SetCursorLocked(bool locked) { m_cursorLocked = locked; }

private:
    struct Gamepad {
        GamepadId id = GamepadId::Invalid;
        std::string name;
        std::bitset<static_cast<std::size_t>(GamepadButton::Count)> down;
        std::bitset<static_cast<std::size_t>(GamepadButton::Count)> pressed;
        std::bitset<static_cast<std::size_t>(GamepadButton::Count)> released;
        std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> axes = {};
    };

    void ReleaseAll();
    // The gamepad's state, added when its first event comes before it was said to connect.
    Gamepad& FindOrAddGamepad(GamepadId id);
    template <typename Test>
    bool AnyGamepadMatches(int gamepad, Test&& test) const;

    std::bitset<256> m_keysDown;
    std::bitset<256> m_keysPressed;
    std::bitset<256> m_keysReleased;

    std::bitset<8> m_buttonsDown;
    std::bitset<8> m_buttonsPressed;
    std::bitset<8> m_buttonsReleased;

    std::vector<Gamepad> m_gamepads;

    Vector2 m_mousePosition;
    Vector2 m_mouseDelta;
    Vector2 m_scrollDelta;
    bool m_cursorLocked = false;
};

} // namespace Spot
