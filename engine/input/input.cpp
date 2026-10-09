#include "input/input.h"

#include "input/input_state.h"

namespace Spot::Input {

namespace {

const InputState EmptyState;
InputState* s_state = nullptr;

const InputState& GetCurrent() {
    return s_state ? *s_state : EmptyState;
}

} // namespace

void SetState(InputState* state) {
    s_state = state;
}

InputState* GetState() {
    return s_state;
}

bool IsKeyDown(Key key) {
    return GetCurrent().IsKeyDown(key);
}

bool IsKeyPressed(Key key) {
    return GetCurrent().IsKeyPressed(key);
}

bool IsKeyReleased(Key key) {
    return GetCurrent().IsKeyReleased(key);
}

bool IsMouseButtonDown(MouseButton button) {
    return GetCurrent().IsMouseButtonDown(button);
}

bool IsMouseButtonPressed(MouseButton button) {
    return GetCurrent().IsMouseButtonPressed(button);
}

bool IsMouseButtonReleased(MouseButton button) {
    return GetCurrent().IsMouseButtonReleased(button);
}

Vector2 GetMousePosition() {
    return GetCurrent().GetMousePosition();
}

Vector2 GetMouseDelta() {
    return GetCurrent().GetMouseDelta();
}

Vector2 GetScrollDelta() {
    return GetCurrent().GetScrollDelta();
}

void SetCursorLocked(bool locked) {
    if (s_state) {
        s_state->SetCursorLocked(locked);
    }
}

bool IsCursorLocked() {
    return GetCurrent().IsCursorLocked();
}

} // namespace Spot::Input
