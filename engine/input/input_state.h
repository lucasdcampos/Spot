#pragma once

#include "events/event.h"
#include "input/keys.h"

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

    float GetMouseX() const { return m_mouseX; }
    float GetMouseY() const { return m_mouseY; }
    float GetMouseDeltaX() const { return m_mouseDeltaX; }
    float GetMouseDeltaY() const { return m_mouseDeltaY; }
    float GetScrollDeltaX() const { return m_scrollDeltaX; }
    float GetScrollDeltaY() const { return m_scrollDeltaY; }

private:
    void ReleaseAll();

    std::bitset<256> m_keysDown;
    std::bitset<256> m_keysPressed;
    std::bitset<256> m_keysReleased;

    std::bitset<8> m_buttonsDown;
    std::bitset<8> m_buttonsPressed;
    std::bitset<8> m_buttonsReleased;

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    float m_scrollDeltaX = 0.0f;
    float m_scrollDeltaY = 0.0f;
};

} // namespace Spot
