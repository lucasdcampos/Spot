#pragma once

#include "spot/core/math/vector.h"
#include "spot/platform/keys.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

class Archive;
class InputState;

enum class InputActionType : std::uint8_t {
    // On or off, such as Jump, with presses and releases.
    Button,
    // A value from -1 to 1, such as Throttle.
    Axis,
    // Two axes, such as Move or Look.
    Vector2,
};

// What a binding reads.
enum class InputSource : std::uint8_t {
    Key,
    MouseButton,
    // The mouse's movement this frame, in pixels, right and up positive; the code is its InputAxes.
    MouseMotion,
    // The wheel's movement this frame, in notches, right and away from the user positive; the code is its InputAxes.
    Scroll,
    GamepadButton,
    GamepadAxis,
    // Both axes of a stick: code 0 is the left stick, 1 the right one.
    GamepadStick,
    // Buttons making up an axis or a vector; see InputBinding::parts.
    Composite,
};

// Which axes of the mouse or the wheel a control reads: actions of one axis read X, or Y when it is the only one.
enum class InputAxes : std::uint8_t { Both, X, Y };

// One control: a key, a button, an axis, a stick, the mouse or the wheel.
struct InputControl {
    InputSource source = InputSource::Key;
    // The Key, MouseButton, GamepadButton or GamepadAxis, or the stick.
    int code = 0;

    static InputControl Of(Key key) { return {InputSource::Key, static_cast<int>(key)}; }
    static InputControl Of(MouseButton button) { return {InputSource::MouseButton, static_cast<int>(button)}; }
    static InputControl Of(GamepadButton button) { return {InputSource::GamepadButton, static_cast<int>(button)}; }
    static InputControl Of(GamepadAxis axis) { return {InputSource::GamepadAxis, static_cast<int>(axis)}; }
    static InputControl MouseMotion(InputAxes axes = InputAxes::Both) {
        return {InputSource::MouseMotion, static_cast<int>(axes)};
    }
    static InputControl Scroll(InputAxes axes = InputAxes::Both) {
        return {InputSource::Scroll, static_cast<int>(axes)};
    }
    // Code 0 is the left stick, 1 the right one.
    static InputControl GamepadStick(int stick) { return {InputSource::GamepadStick, stick}; }

    // How players see it, such as "W", "Mouse Left" or "Gamepad South".
    std::string GetName() const;
    void Serialize(Archive& archive);
    friend bool operator==(const InputControl&, const InputControl&) = default;
};

// A control an action reads, and how its value is processed: values within the dead zone count as 0 and the rest is
// stretched to fill the range, then they are inverted where asked and scaled.
struct InputBinding {
    InputControl control;
    // Composites only: up, down, left and right for vectors; positive and negative, the first two, for axes. Each is
    // a key, a mouse button or a gamepad button.
    std::array<InputControl, 4> parts = {};
    float deadZone = 0.0f;
    float scale = 1.0f;
    bool invertX = false;
    bool invertY = false;

    static InputBinding Of(InputControl control) { return {.control = control}; }
    static InputBinding Composite(InputControl up, InputControl down, InputControl left, InputControl right);
    static InputBinding Composite(InputControl positive, InputControl negative);

    void Serialize(Archive& archive);
    friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct InputAction {
    std::string name;
    InputActionType type = InputActionType::Button;
    std::vector<InputBinding> bindings = {};
    // What ResetBindings() goes back to, and saved rebinds are compared with; see InputActionMap::SetDefaults().
    std::vector<InputBinding> defaultBindings = {};
    // How far an axis must go for a button action to count as down.
    float pressPoint = 0.5f;

    InputAction& Bind(InputBinding binding);
    InputAction& Bind(InputControl control) { return Bind(InputBinding::Of(control)); }
    void Serialize(Archive& archive);
};

// Actions a game asks about by name, such as Jump or Move, whatever controls the player uses for them:
//
//     map.Add("Jump", Spot::InputActionType::Button)
//         .Bind(Spot::InputControl::Of(Spot::Key::Space))
//         .Bind(Spot::InputControl::Of(Spot::GamepadButton::South));
//
//     if (Spot::Input::IsActionPressed("Jump")) { ... }
//
// Update() reads the input state once per frame: an action's value is that of the binding farthest from rest, and
// presses and releases are the frames its button went down or up. The application keeps the game's map, from its
// input settings, and updates it after each frame's events; games may also make and update their own.
class InputActionMap {
public:
    // A new action, or the one of that name, changed to that type.
    InputAction& Add(std::string name, InputActionType type);
    bool Remove(std::string_view name);
    InputAction* Find(std::string_view name);
    const InputAction* Find(std::string_view name) const;
    std::span<const InputAction> GetActions() const { return m_actions; }
    std::vector<InputAction>& GetActions() { return m_actions; }

    // Which gamepad the bindings read; InputState::AnyGamepad, every one, by default.
    int GetGamepad() const { return m_gamepad; }
    void SetGamepad(int gamepad) { m_gamepad = gamepad; }

    void Update(const InputState& state);

    // Rebinding, as games let players do. The defaults are each action's bindings when SetDefaults() was last called,
    // which the application does once it has applied the game's input settings.
    void SetDefaults();
    // Replaces a binding's control, or with a part from 0 to 3, that part of a composite. False when the action or
    // the binding does not exist.
    bool Rebind(std::string_view action, std::size_t binding, InputControl control, int part = -1);
    // Whether the action's bindings differ from its defaults.
    bool IsRebound(std::string_view action) const;
    void ResetBindings();
    bool ResetBindings(std::string_view action);
    // The rebound actions' bindings, as JSON text, so a game update's new defaults reach the actions players left.
    std::string SaveRebindsToString() const;
    // Applies saved rebinds to the actions the map has; others are ignored. False, changing nothing, when the text
    // holds no rebinds.
    bool LoadRebindsFromString(std::string_view text);
    bool SaveRebinds(const std::filesystem::path& file) const;
    // False, after logging why, when the file exists and cannot be read; a missing file loads nothing.
    bool LoadRebinds(const std::filesystem::path& file);

    // False, or 0, for actions the map does not have.
    bool IsDown(std::string_view action) const;
    bool IsPressed(std::string_view action) const;
    bool IsReleased(std::string_view action) const;
    float GetValue(std::string_view action) const;
    Vector2 GetVector2(std::string_view action) const;

    void Serialize(Archive& archive);

private:
    struct State {
        bool down = false;
        bool pressed = false;
        bool released = false;
        Vector2 value = {};
    };

    const State* FindState(std::string_view action) const;

    std::vector<InputAction> m_actions;
    // By action, in the same order, from the last update.
    std::vector<State> m_states;
    int m_gamepad = -1;
};

// The control the player pressed this frame, as rebinding captures it: a key, a mouse button or a gamepad button that
// went down, or else a gamepad axis pushed more than half way. Nothing when there is none.
std::optional<InputControl> FindPressedControl(const InputState& state);

} // namespace Spot
