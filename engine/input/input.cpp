#include "input/input.h"

#include "input/input_state.h"

namespace Spot::Input {

namespace {

const InputState EmptyState;
const InputState* s_state = nullptr;

const InputState& GetState() {
    return s_state ? *s_state : EmptyState;
}

} // namespace

void SetState(const InputState* state) {
    s_state = state;
}

bool IsKeyDown(Key key) {
    return GetState().IsKeyDown(key);
}

bool IsKeyPressed(Key key) {
    return GetState().IsKeyPressed(key);
}

bool IsKeyReleased(Key key) {
    return GetState().IsKeyReleased(key);
}

bool IsMouseButtonDown(MouseButton button) {
    return GetState().IsMouseButtonDown(button);
}

bool IsMouseButtonPressed(MouseButton button) {
    return GetState().IsMouseButtonPressed(button);
}

bool IsMouseButtonReleased(MouseButton button) {
    return GetState().IsMouseButtonReleased(button);
}

Vector2 GetMousePosition() {
    return GetState().GetMousePosition();
}

Vector2 GetMouseDelta() {
    return GetState().GetMouseDelta();
}

Vector2 GetScrollDelta() {
    return GetState().GetScrollDelta();
}

} // namespace Spot::Input
