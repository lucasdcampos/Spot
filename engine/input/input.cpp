#include "input/input.h"

#include "input/input_state.h"
#include "platform/platform.h"

#include <chrono>

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

std::string_view GetTypedText() {
    return GetCurrent().GetTypedText();
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

int GetGamepadCount() {
    return GetCurrent().GetGamepadCount();
}

bool IsGamepadButtonDown(GamepadButton button, int gamepad) {
    return GetCurrent().IsGamepadButtonDown(button, gamepad);
}

bool IsGamepadButtonPressed(GamepadButton button, int gamepad) {
    return GetCurrent().IsGamepadButtonPressed(button, gamepad);
}

bool IsGamepadButtonReleased(GamepadButton button, int gamepad) {
    return GetCurrent().IsGamepadButtonReleased(button, gamepad);
}

float GetGamepadAxis(GamepadAxis axis, int gamepad) {
    return GetCurrent().GetGamepadAxis(axis, gamepad);
}

bool RumbleGamepad(float low, float high, float seconds, int gamepad) {
    const InputState& state = GetCurrent();
    const auto duration = std::chrono::milliseconds(static_cast<std::int64_t>(seconds * 1000.0f));
    bool rumbled = false;
    for (int i = 0; i < state.GetGamepadCount(); ++i) {
        if (gamepad == AnyGamepad || gamepad == i) {
            rumbled = Platform::RumbleGamepad(state.GetGamepadId(i), low, high, duration) || rumbled;
        }
    }
    return rumbled;
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
