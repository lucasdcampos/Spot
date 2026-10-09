#pragma once

#include "input/keys.h"
#include "math/vector.h"

namespace Spot {

class InputState;

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

Vector2 GetMousePosition();
Vector2 GetMouseDelta();
Vector2 GetScrollDelta();

// Hides the cursor and keeps it in place while locked, so the mouse only moves things, as it turns first-person
// views; see InputState::SetCursorLocked(). Does nothing without a state.
void SetCursorLocked(bool locked);
bool IsCursorLocked();

} // namespace Input

} // namespace Spot
