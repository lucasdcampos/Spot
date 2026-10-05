#pragma once

#include "input/keys.h"

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

float GetMouseX();
float GetMouseY();
float GetMouseDeltaX();
float GetMouseDeltaY();
float GetScrollDeltaX();
float GetScrollDeltaY();

} // namespace Input

} // namespace Spot
