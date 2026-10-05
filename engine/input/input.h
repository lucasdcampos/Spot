#pragma once

#include "input/keys.h"
#include "math/vector.h"

namespace Spot {

class InputState;

// Global access to the input of the running application, e.g. Spot::Input::IsKeyDown(Spot::Key::W).
namespace Input {

// The application points this at the state it updates every frame. Without a state every query reports
// nothing held and no movement.
void SetState(const InputState* state);

bool IsKeyDown(Key key);
bool IsKeyPressed(Key key);
bool IsKeyReleased(Key key);

bool IsMouseButtonDown(MouseButton button);
bool IsMouseButtonPressed(MouseButton button);
bool IsMouseButtonReleased(MouseButton button);

Vector2 GetMousePosition();
Vector2 GetMouseDelta();
Vector2 GetScrollDelta();

} // namespace Input

} // namespace Spot
