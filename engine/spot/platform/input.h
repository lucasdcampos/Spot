#pragma once

#include "spot/core/math/vector.h"
#include "spot/platform/input_state.h"
#include "spot/platform/keys.h"

#include <string_view>

namespace Spot {

class InputActionMap;

// Global access to the input of the running application, e.g. Spot::Input::IsKeyDown(Spot::Key::W).
namespace Input {

// The application points this at the state it updates every frame. Without a state every query reports
// nothing held and no movement.
void SetState(InputState* state);
// The state queries read; null when there is none.
InputState* GetState();

bool IsKeyDown(Key key);
bool IsKeyPressed(Key key);
bool IsKeyReleased(Key key);

bool IsMouseButtonDown(MouseButton button);
bool IsMouseButtonPressed(MouseButton button);
bool IsMouseButtonReleased(MouseButton button);

// The text typed this frame while text input was on, as UTF-8; see Window::StartTextInput().
std::string_view GetTypedText();

Vector2 GetMousePosition();
Vector2 GetMouseDelta();
Vector2 GetScrollDelta();

// Gamepads; see InputState. With AnyGamepad, queries look at every gamepad.
inline constexpr int AnyGamepad = -1;
int GetGamepadCount();
bool IsGamepadButtonDown(GamepadButton button, int gamepad = AnyGamepad);
bool IsGamepadButtonPressed(GamepadButton button, int gamepad = AnyGamepad);
bool IsGamepadButtonReleased(GamepadButton button, int gamepad = AnyGamepad);
float GetGamepadAxis(GamepadAxis axis, int gamepad = AnyGamepad);
// Shakes the gamepad's low and high frequency motors, each from 0 to 1, for the seconds; with AnyGamepad, every
// gamepad's. A new call replaces the last. Returns whether a gamepad rumbles.
bool RumbleGamepad(float low, float high, float seconds, int gamepad = AnyGamepad);

// Actions; see InputActionMap. The application points this at its map, updated after each frame's events. Without
// a map every action reports nothing.
void SetActionMap(InputActionMap* map);
InputActionMap* GetActionMap();
// Updates the map from the state; the application calls it once per frame.
void UpdateActions();
bool IsActionDown(std::string_view action);
bool IsActionPressed(std::string_view action);
bool IsActionReleased(std::string_view action);
float GetActionValue(std::string_view action);
Vector2 GetActionVector2(std::string_view action);

// Hides the cursor and keeps it in place while locked, so the mouse only moves things, as it turns first-person
// views; see InputState::SetCursorLocked(). Does nothing without a state.
void SetCursorLocked(bool locked);
bool IsCursorLocked();

// Overlays take the input from the game while they need it; see InputState::PushCapture(). Without a state, captures
// are InputCaptureId::None and do nothing.
InputCaptureId PushCapture(std::string_view owner);
void PopCapture(InputCaptureId capture);
bool IsCaptured();

} // namespace Input

} // namespace Spot
