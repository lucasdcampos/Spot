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

float GetMouseX() {
    return GetState().GetMouseX();
}

float GetMouseY() {
    return GetState().GetMouseY();
}

float GetMouseDeltaX() {
    return GetState().GetMouseDeltaX();
}

float GetMouseDeltaY() {
    return GetState().GetMouseDeltaY();
}

float GetScrollDeltaX() {
    return GetState().GetScrollDeltaX();
}

float GetScrollDeltaY() {
    return GetState().GetScrollDeltaY();
}

} // namespace Spot::Input
