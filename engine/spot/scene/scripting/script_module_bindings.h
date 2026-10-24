#pragma once

#include "spot/scene/scripting/script_scene.h"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace Spot::Detail {

template <typename T>
T Argument(const ScriptValue& value) {
    return FromScriptValue<T>(value);
}
inline void Count(std::span<const ScriptValue> arguments, std::size_t count) {
    if (arguments.size() != count)
        throw std::runtime_error("Incorrect argument count");
}

template <std::size_t N>
constexpr bool Contains(const std::array<std::string_view, N>& names, std::string_view name) {
    for (const auto item : names) {
        if (item == name)
            return true;
    }
    return false;
}
// Finds the enum value named as GetKeyName() and the like name it, for the first `count` values.
template <typename T, typename Name>
T ParseName(std::string_view text, int first, int last, Name name, std::string_view what) {
    for (int i = first; i <= last; ++i) {
        if (name(static_cast<T>(i)) == text)
            return static_cast<T>(i);
    }
    throw std::runtime_error("Unknown " + std::string(what) + " name '" + std::string(text) + "'");
}

// A value object with fixed read-only properties, such as a contact or a raycast hit. Points beyond the first are read
// with GetPoint(index), since script values have no engine array type.
struct ScriptRecord final : ScriptObject {
    std::unordered_map<std::string, ScriptValue> properties;
    std::vector<ScriptValue> points;
    bool HasProperty(std::string_view name) const override { return properties.contains(std::string(name)); }
    bool HasMethod(std::string_view name) const override { return name == "GetPoint" && !points.empty(); }
    ScriptValue Get(std::string_view name) const override {
        if (const auto found = properties.find(std::string(name)); found != properties.end())
            return found->second;
        return ScriptObject::Get(name);
    }
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override {
        if (name == "GetPoint" && !points.empty()) {
            Count(arguments, 1);
            const auto index = Argument<std::uint32_t>(arguments[0]);
            if (index >= points.size())
                throw std::runtime_error("Contact point index is out of range");
            return points[index];
        }
        return ScriptObject::Call(name, arguments);
    }
};

enum class SceneProperty : std::size_t {
    Unknown,
    Scene,
    Transform,
    Parent,
    Name,
    Id,
    Active,
    ActiveInHierarchy,
    Layer,
    Entity,
    Position,
    WorldPosition,
    Scale,
    Rotation,
    WorldRotation,
    Rotation2D,
    Enabled,
    Faulted,
    DeltaTime,
    UnscaledDeltaTime,
    Time,
    UnscaledTime,
    FrameCount,
    FixedDeltaTime,
    TimeScale,
    MousePosition,
    MouseDelta,
    ScrollDelta,
    GamepadCount,
    CursorLocked,
    Count
};
template <typename T>
void PropertyMethods(std::string_view property, std::string_view getter, std::string_view setter = {}) {
    SetScriptMethod(
        typeid(T), std::string(getter),
        [name = std::string(property)](Component& component, std::span<const ScriptValue> arguments) {
            Count(arguments, 0);
            return GetScriptProperty(component, name);
        },
        GetScriptPropertyType(typeid(T), property));
    if (!setter.empty())
        SetScriptMethod(
            typeid(T), std::string(setter),
            [name = std::string(property)](Component& component,
                                           std::span<const ScriptValue> arguments) -> ScriptValue {
                Count(arguments, 1);
                WriteScriptProperty(component, name, arguments[0]);
                return {};
            },
            "");
}

void RegisterRenderingScriptBindings();
void RegisterAudioScriptBindings();
ScriptValue CallScriptInput(std::string_view name, std::span<const ScriptValue> arguments);
ScriptValue GetScriptInput(SceneProperty property, std::string_view name);
void SetScriptCursorLocked(const ScriptValue& value);
ScriptValue GetScriptTime(SceneProperty property, std::string_view name);
void SetScriptTimeScale(const ScriptValue& value);
EntityId ReadScriptEntityId(const ScriptValue& value);
ScriptValue MakeScriptReference(const std::shared_ptr<ScriptSceneContext>& context, EntityId entity,
                                ComponentId component = {});
ScriptValue CallScriptPhysics(Scene& scene, const std::shared_ptr<ScriptSceneContext>& context, std::string_view name,
                              std::span<const ScriptValue> arguments);

} // namespace Spot::Detail
