#pragma once

#include "events/event.h"
#include "input/keys.h"
#include "math/vector.h"

#include <bitset>

namespace Spot {

// Keyboard and mouse state built from events. "Pressed" and "released" only hold during the frame in which
// the change happened, so a press and release within one frame are both still seen.
class InputState {
public:
    // Ends the previous frame: clears per-frame transitions and accumulated deltas.
    void BeginFrame();
    void OnEvent(const Event& event);

    bool IsKeyDown(Key key) const;
    bool IsKeyPressed(Key key) const;
    bool IsKeyReleased(Key key) const;

    bool IsMouseButtonDown(MouseButton button) const;
    bool IsMouseButtonPressed(MouseButton button) const;
    bool IsMouseButtonReleased(MouseButton button) const;

    Vector2 GetMousePosition() const { return m_mousePosition; }
    Vector2 GetMouseDelta() const { return m_mouseDelta; }
    Vector2 GetScrollDelta() const { return m_scrollDelta; }

    // Whether the game asked for the cursor to be hidden and kept in place, so the mouse only moves things, as it
    // turns first-person views. The application applies it to its window; tools running a game in a view of their
    // own, such as the editor, apply it while that view has focus. Events leave it as it is.
    bool IsCursorLocked() const { return m_cursorLocked; }
    void SetCursorLocked(bool locked) { m_cursorLocked = locked; }

private:
    void ReleaseAll();

    std::bitset<256> m_keysDown;
    std::bitset<256> m_keysPressed;
    std::bitset<256> m_keysReleased;

    std::bitset<8> m_buttonsDown;
    std::bitset<8> m_buttonsPressed;
    std::bitset<8> m_buttonsReleased;

    Vector2 m_mousePosition;
    Vector2 m_mouseDelta;
    Vector2 m_scrollDelta;
    bool m_cursorLocked = false;
};

} // namespace Spot
