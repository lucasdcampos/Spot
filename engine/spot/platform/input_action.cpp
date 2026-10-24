#include "spot/platform/input_action.h"

#include "spot/core/debug/log.h"
#include "spot/core/io/archive.h"
#include "spot/core/io/archive_state.h"
#include "spot/core/io/file_io.h"
#include "spot/core/io/paths.h"
#include "spot/platform/input_state.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>

namespace Spot {

namespace {

// A control's value this frame, and whether a button went down or up during it.
struct Reading {
    Vector2 value = {};
    bool pressed = false;
    bool released = false;
};

// The axes of the mouse or the wheel the control reads.
Vector2 KeepAxes(Vector2 value, int code) {
    switch (static_cast<InputAxes>(code)) {
    case InputAxes::X:
        return {value.x, 0.0f};
    case InputAxes::Y:
        return {0.0f, value.y};
    case InputAxes::Both:
        break;
    }
    return value;
}

std::string_view GetAxesSuffix(int code) {
    switch (static_cast<InputAxes>(code)) {
    case InputAxes::X:
        return " X";
    case InputAxes::Y:
        return " Y";
    case InputAxes::Both:
        break;
    }
    return "";
}

Reading Read(const InputControl& control, const InputState& state, int gamepad) {
    Reading reading;
    switch (control.source) {
    case InputSource::Key: {
        const auto key = static_cast<Key>(control.code);
        reading = {{state.IsKeyDown(key) ? 1.0f : 0.0f, 0.0f}, state.IsKeyPressed(key), state.IsKeyReleased(key)};
        break;
    }
    case InputSource::MouseButton: {
        const auto button = static_cast<MouseButton>(control.code);
        reading = {{state.IsMouseButtonDown(button) ? 1.0f : 0.0f, 0.0f},
                   state.IsMouseButtonPressed(button),
                   state.IsMouseButtonReleased(button)};
        break;
    }
    case InputSource::GamepadButton: {
        const auto button = static_cast<GamepadButton>(control.code);
        reading = {{state.IsGamepadButtonDown(button, gamepad) ? 1.0f : 0.0f, 0.0f},
                   state.IsGamepadButtonPressed(button, gamepad),
                   state.IsGamepadButtonReleased(button, gamepad)};
        break;
    }
    case InputSource::MouseMotion: {
        // Screens count down; actions count up, as sticks do.
        const Vector2 delta = state.GetMouseDelta();
        reading.value = KeepAxes({delta.x, -delta.y}, control.code);
        break;
    }
    case InputSource::Scroll:
        reading.value = KeepAxes(state.GetScrollDelta(), control.code);
        break;
    case InputSource::GamepadAxis:
        reading.value.x = state.GetGamepadAxis(static_cast<GamepadAxis>(control.code), gamepad);
        break;
    case InputSource::GamepadStick: {
        const bool left = control.code == 0;
        reading.value = {state.GetGamepadAxis(left ? GamepadAxis::LeftX : GamepadAxis::RightX, gamepad),
                         state.GetGamepadAxis(left ? GamepadAxis::LeftY : GamepadAxis::RightY, gamepad)};
        break;
    }
    case InputSource::Composite:
        break;
    }
    return reading;
}

// Values within the dead zone become 0, and the rest is stretched to fill the range again.
float ApplyDeadZone(float magnitude, float deadZone) {
    if (magnitude <= deadZone) {
        return 0.0f;
    }
    return deadZone > 0.0f && deadZone < 1.0f ? (magnitude - deadZone) / (1.0f - deadZone) : magnitude;
}

Reading ReadBinding(const InputBinding& binding, InputActionType type, const InputState& state, int gamepad) {
    Reading reading;
    if (binding.control.source == InputSource::Composite) {
        std::array<Reading, 4> parts;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            parts[i] = Read(binding.parts[i], state, gamepad);
            reading.pressed = reading.pressed || parts[i].pressed;
            reading.released = reading.released || parts[i].released;
        }
        if (type == InputActionType::Vector2) {
            reading.value = {parts[3].value.x - parts[2].value.x, parts[0].value.x - parts[1].value.x};
            // Diagonals of four buttons go no faster than a stick pushed all the way.
            if (LengthSquared(reading.value) > 1.0f) {
                reading.value = Normalize(reading.value);
            }
        } else {
            reading.value.x = parts[0].value.x - parts[1].value.x;
        }
    } else {
        reading = Read(binding.control, state, gamepad);
    }

    if (type == InputActionType::Vector2) {
        const float length = Length(reading.value);
        reading.value = length > 0.0f ? reading.value * (ApplyDeadZone(length, binding.deadZone) / length) : Vector2{};
    } else {
        const bool onlyY =
            (binding.control.source == InputSource::MouseMotion || binding.control.source == InputSource::Scroll) &&
            static_cast<InputAxes>(binding.control.code) == InputAxes::Y;
        const float value = onlyY ? reading.value.y : reading.value.x;
        reading.value = {std::copysign(ApplyDeadZone(std::abs(value), binding.deadZone), value), 0.0f};
    }
    reading.value.x *= binding.invertX ? -binding.scale : binding.scale;
    reading.value.y *= binding.invertY ? -binding.scale : binding.scale;
    return reading;
}

constexpr std::string_view RebindsFormat = "spot-input-rebinds";
constexpr int RebindsFormatVersion = 1;

// What a rebinds file holds of an action.
struct Rebind {
    std::string action;
    std::vector<InputBinding> bindings;

    void Serialize(Archive& archive) {
        archive.Field("action", action);
        archive.Field("bindings", bindings);
    }
};

float Magnitude(Vector2 value, InputActionType type) {
    return type == InputActionType::Vector2 ? Length(value) : std::abs(value.x);
}

} // namespace

std::string InputControl::GetName() const {
    switch (source) {
    case InputSource::Key:
        return std::string(GetKeyName(static_cast<Key>(code)));
    case InputSource::MouseButton:
        return "Mouse " + std::string(GetMouseButtonName(static_cast<MouseButton>(code)));
    case InputSource::MouseMotion:
        return "Mouse" + std::string(GetAxesSuffix(code));
    case InputSource::Scroll:
        return "Mouse Wheel" + std::string(GetAxesSuffix(code));
    case InputSource::GamepadButton:
        return "Gamepad " + std::string(GetGamepadButtonName(static_cast<GamepadButton>(code)));
    case InputSource::GamepadAxis:
        return "Gamepad " + std::string(GetGamepadAxisName(static_cast<GamepadAxis>(code)));
    case InputSource::GamepadStick:
        return code == 0 ? "Gamepad Left Stick" : "Gamepad Right Stick";
    case InputSource::Composite:
        return "Composite";
    }
    return {};
}

void InputControl::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 8> Sources = {
        "Key", "MouseButton", "MouseMotion", "Scroll", "GamepadButton", "GamepadAxis", "GamepadStick", "Composite"};
    archive.Field("source", source, {.enumNames = Sources});
    archive.Field("code", code);
}

InputBinding InputBinding::Composite(InputControl up, InputControl down, InputControl left, InputControl right) {
    return {.control = {InputSource::Composite, 0}, .parts = {up, down, left, right}};
}

InputBinding InputBinding::Composite(InputControl positive, InputControl negative) {
    return {.control = {InputSource::Composite, 0}, .parts = {positive, negative, InputControl{}, InputControl{}}};
}

void InputBinding::Serialize(Archive& archive) {
    archive.Field("control", control);
    if (control.source == InputSource::Composite) {
        std::vector<InputControl> list(parts.begin(), parts.end());
        archive.Field("parts", list);
        for (std::size_t i = 0; i < parts.size() && i < list.size(); ++i) {
            parts[i] = list[i];
        }
    }
    archive.Field("deadZone", deadZone, {.min = 0.0f, .max = 0.99f, .speed = 0.01f});
    archive.Field("scale", scale, {.speed = 0.01f});
    archive.Field("invertX", invertX);
    archive.Field("invertY", invertY);
}

InputAction& InputAction::Bind(InputBinding binding) {
    bindings.push_back(binding);
    return *this;
}

void InputAction::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 3> Types = {"Button", "Axis", "Vector2"};
    archive.Field("name", name);
    archive.Field("type", type, {.enumNames = Types});
    archive.Field("pressPoint", pressPoint, {.min = 0.01f, .max = 1.0f, .speed = 0.01f});
    archive.Field("bindings", bindings);
}

InputAction& InputActionMap::Add(std::string name, InputActionType type) {
    if (InputAction* found = Find(name)) {
        found->type = type;
        return *found;
    }
    return m_actions.emplace_back(InputAction{.name = std::move(name), .type = type});
}

bool InputActionMap::Remove(std::string_view name) {
    const auto found = std::ranges::find(m_actions, name, &InputAction::name);
    if (found == m_actions.end()) {
        return false;
    }
    m_actions.erase(found);
    // The states follow the actions by place, so they start over.
    m_states.clear();
    return true;
}

InputAction* InputActionMap::Find(std::string_view name) {
    const auto found = std::ranges::find(m_actions, name, &InputAction::name);
    return found != m_actions.end() ? &*found : nullptr;
}

const InputAction* InputActionMap::Find(std::string_view name) const {
    const auto found = std::ranges::find(m_actions, name, &InputAction::name);
    return found != m_actions.end() ? &*found : nullptr;
}

void InputActionMap::Update(const InputState& state) {
    m_states.resize(m_actions.size());
    for (std::size_t i = 0; i < m_actions.size(); ++i) {
        const InputAction& action = m_actions[i];
        State& actionState = m_states[i];
        Vector2 value = {};
        bool pressedEvent = false;
        bool releasedEvent = false;
        for (const InputBinding& binding : action.bindings) {
            const Reading reading = ReadBinding(binding, action.type, state, m_gamepad);
            if (Magnitude(reading.value, action.type) > Magnitude(value, action.type)) {
                value = reading.value;
            }
            pressedEvent = pressedEvent || reading.pressed;
            releasedEvent = releasedEvent || reading.released;
        }
        const bool wasDown = actionState.down;
        const bool down = Magnitude(value, action.type) >= action.pressPoint;
        actionState.value = value;
        actionState.down = down;
        // A press and a release within the frame are both seen, as the input state sees them, in either order.
        const bool bounced = pressedEvent && releasedEvent && wasDown == down;
        actionState.pressed = (!wasDown && down) || bounced;
        actionState.released = (wasDown && !down) || bounced;
    }
}

const InputActionMap::State* InputActionMap::FindState(std::string_view action) const {
    for (std::size_t i = 0; i < m_actions.size() && i < m_states.size(); ++i) {
        if (m_actions[i].name == action) {
            return &m_states[i];
        }
    }
    return nullptr;
}

bool InputActionMap::IsDown(std::string_view action) const {
    const State* state = FindState(action);
    return state && state->down;
}

bool InputActionMap::IsPressed(std::string_view action) const {
    const State* state = FindState(action);
    return state && state->pressed;
}

bool InputActionMap::IsReleased(std::string_view action) const {
    const State* state = FindState(action);
    return state && state->released;
}

float InputActionMap::GetValue(std::string_view action) const {
    const State* state = FindState(action);
    return state ? state->value.x : 0.0f;
}

Vector2 InputActionMap::GetVector2(std::string_view action) const {
    const State* state = FindState(action);
    return state ? state->value : Vector2{};
}

std::optional<InputControl> FindPressedControl(const InputState& state) {
    for (int code = static_cast<int>(Key::Unknown) + 1; code <= static_cast<int>(Key::RightSuper); ++code) {
        if (state.IsKeyPressed(static_cast<Key>(code))) {
            return InputControl::Of(static_cast<Key>(code));
        }
    }
    for (int code = 0; code <= static_cast<int>(MouseButton::X2); ++code) {
        if (state.IsMouseButtonPressed(static_cast<MouseButton>(code))) {
            return InputControl::Of(static_cast<MouseButton>(code));
        }
    }
    for (int code = 0; code < static_cast<int>(GamepadButton::Count); ++code) {
        if (state.IsGamepadButtonPressed(static_cast<GamepadButton>(code))) {
            return InputControl::Of(static_cast<GamepadButton>(code));
        }
    }
    for (int code = 0; code < static_cast<int>(GamepadAxis::Count); ++code) {
        if (std::abs(state.GetGamepadAxis(static_cast<GamepadAxis>(code))) > 0.5f) {
            return InputControl::Of(static_cast<GamepadAxis>(code));
        }
    }
    return std::nullopt;
}

void InputActionMap::SetDefaults() {
    for (InputAction& action : m_actions) {
        action.defaultBindings = action.bindings;
    }
}

bool InputActionMap::Rebind(std::string_view action, std::size_t binding, InputControl control, int part) {
    InputAction* found = Find(action);
    if (!found || binding >= found->bindings.size() || part >= 4) {
        return false;
    }
    InputBinding& rebound = found->bindings[binding];
    (part < 0 ? rebound.control : rebound.parts[static_cast<std::size_t>(part)]) = control;
    return true;
}

bool InputActionMap::IsRebound(std::string_view action) const {
    const InputAction* found = Find(action);
    return found && found->bindings != found->defaultBindings;
}

void InputActionMap::ResetBindings() {
    for (InputAction& action : m_actions) {
        action.bindings = action.defaultBindings;
    }
}

bool InputActionMap::ResetBindings(std::string_view action) {
    InputAction* found = Find(action);
    if (!found) {
        return false;
    }
    found->bindings = found->defaultBindings;
    return true;
}

std::string InputActionMap::SaveRebindsToString() const {
    std::vector<Spot::Rebind> rebinds;
    for (const InputAction& action : m_actions) {
        if (action.bindings != action.defaultBindings) {
            rebinds.push_back({action.name, action.bindings});
        }
    }
    nlohmann::json document = {{"format", RebindsFormat}, {"formatVersion", RebindsFormatVersion}};
    Detail::ArchiveState state{.loading = false, .nodes = {&document}};
    Archive archive(state);
    archive.Field("actions", rebinds);
    return document.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
}

bool InputActionMap::LoadRebindsFromString(std::string_view text) {
    nlohmann::json document = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object() || document.value("format", "") != RebindsFormat) {
        return false;
    }
    const auto version = document.find("formatVersion");
    if (version == document.end() || !version->is_number_integer() || version->get<int>() > RebindsFormatVersion) {
        return false;
    }
    std::vector<Spot::Rebind> rebinds;
    Detail::ArchiveState state{.loading = true, .nodes = {&document}};
    Archive archive(state);
    archive.Field("actions", rebinds);
    for (Spot::Rebind& rebind : rebinds) {
        if (InputAction* action = Find(rebind.action)) {
            action->bindings = std::move(rebind.bindings);
        }
    }
    return true;
}

bool InputActionMap::SaveRebinds(const std::filesystem::path& file) const {
    std::ofstream stream(file, std::ios::binary);
    stream << SaveRebindsToString();
    if (!stream) {
        Log::GetEngineLogger().Error("Could not write the input rebinds '{}'", PathToUtf8(file));
        return false;
    }
    return true;
}

bool InputActionMap::LoadRebinds(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return true;
    }
    const auto bytes = Detail::ReadFileBytes(file);
    if (!bytes || !LoadRebindsFromString({reinterpret_cast<const char*>(bytes->data()), bytes->size()})) {
        Log::GetEngineLogger().Error("Could not read the input rebinds '{}'", PathToUtf8(file));
        return false;
    }
    return true;
}

void InputActionMap::Serialize(Archive& archive) {
    archive.Field("actions", m_actions);
    if (archive.IsLoading()) {
        m_states.clear();
    }
}

} // namespace Spot
