#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace Spot::ScriptApi {

// Shared with editor tooling without linking a scripting runtime.
inline constexpr std::string_view Module = "spot";
inline constexpr std::string_view Entity = "entity";
inline constexpr std::string_view Scene = "Scene";
inline constexpr std::string_view Input = "Input";
inline constexpr std::string_view Time = "Time";
inline constexpr std::string_view Physics = "Physics";
inline constexpr std::string_view Component = "Component";
inline constexpr std::string_view StartHook = "OnStart";
inline constexpr std::string_view UpdateHook = "OnUpdate";

struct Hook {
    std::string_view name;
    std::size_t parameterCount;
    // The type of its parameter, as Types names it, when it has one.
    std::string_view parameterType = {};
};
inline constexpr std::array Hooks{Hook{StartHook, 0},
                                  Hook{"OnEnable", 0},
                                  Hook{"OnDisable", 0},
                                  Hook{"OnFixedUpdate", 1, "number"},
                                  Hook{UpdateHook, 1, "number"},
                                  Hook{"OnLateUpdate", 1, "number"},
                                  Hook{"OnDestroy", 0},
                                  Hook{"OnCollisionEnter", 1, "Collision"},
                                  Hook{"OnCollisionStay", 1, "Collision"},
                                  Hook{"OnCollisionExit", 1, "Collision"},
                                  Hook{"OnTriggerEnter", 1, "TriggerContact"},
                                  Hook{"OnTriggerExit", 1, "TriggerContact"},
                                  Hook{"OnCollisionEnter2D", 1, "Collision2D"},
                                  Hook{"OnCollisionStay2D", 1, "Collision2D"},
                                  Hook{"OnCollisionExit2D", 1, "Collision2D"},
                                  Hook{"OnTriggerEnter2D", 1, "TriggerContact2D"},
                                  Hook{"OnTriggerExit2D", 1, "TriggerContact2D"},
                                  Hook{"OnReload", 0}};

inline constexpr std::array<std::string_view, 3> ValueConstructors{"Vector2", "Vector3", "Color"};

constexpr bool IsValueConstructor(std::string_view name) {
    for (const auto constructor : ValueConstructors) {
        if (constructor == name)
            return true;
    }
    return false;
}

// The members of the objects the engine gives scripts: what the scene bindings answer to, and what the editor
// suggests after a dot. Types are named by these tables, value types ("number", "string", "bool", "Vector2",
// "Vector3", "Color") or "any"; components add the members their script bindings describe.
enum class MemberKind { Property, Method };

struct Member {
    std::string_view name;
    MemberKind kind = MemberKind::Property;
    // What reading the property, or calling the method, gives; empty when a method gives nothing.
    std::string_view type = {};
    // Properties: whether scripts can assign it. Methods: whether it sets `property` rather than reading it.
    bool writable = false;
    // Methods: their parameters as people read them, optional ones in brackets.
    std::string_view parameters = {};
    // Methods that read or write a property, as GetName() and SetName(value) do `name`.
    std::string_view property = {};
    // Older spellings that still work but are not suggested, such as Transform for transform.
    bool hidden = false;
};

struct Type {
    std::string_view name;
    std::span<const Member> members;
};

namespace Detail {

constexpr Member Property(std::string_view name, std::string_view type, bool writable = false) {
    return {.name = name, .kind = MemberKind::Property, .type = type, .writable = writable};
}
constexpr Member Alias(std::string_view name, std::string_view type) {
    return {.name = name, .kind = MemberKind::Property, .type = type, .hidden = true};
}
constexpr Member Method(std::string_view name, std::string_view type = {}, std::string_view parameters = {}) {
    return {.name = name, .kind = MemberKind::Method, .type = type, .parameters = parameters};
}
constexpr Member Getter(std::string_view name, std::string_view property, std::string_view type) {
    return {.name = name, .kind = MemberKind::Method, .type = type, .property = property};
}
constexpr Member Setter(std::string_view name, std::string_view property) {
    return {.name = name, .kind = MemberKind::Method, .writable = true, .parameters = "value", .property = property};
}

inline constexpr std::array EntityMembers{
    Property("name", "string", true),
    Property("id", "string"),
    Property("active", "bool", true),
    Property("activeInHierarchy", "bool"),
    Property("layer", "number", true),
    Property("transform", "Transform"),
    Property("parent", "Entity"),
    Property("scene", "Scene"),
    Alias("Transform", "Transform"),
    Alias("Parent", "Entity"),
    Getter("GetName", "name", "string"),
    Setter("SetName", "name"),
    Getter("GetId", "id", "string"),
    Getter("IsActiveSelf", "active", "bool"),
    Setter("SetActive", "active"),
    Getter("IsActiveInHierarchy", "activeInHierarchy", "bool"),
    Getter("GetLayer", "layer", "number"),
    Setter("SetLayer", "layer"),
    Getter("GetTransform", "transform", "Transform"),
    Getter("GetParent", "parent", "Entity"),
    Method("GetScene", "Scene"),
    Method("IsValid", "bool"),
    Method("GetComponent", "Component", "typeName[, index]"),
    Method("HasComponent", "bool", "typeName"),
    Method("FindChild", "Entity", "name"),
    Method("HasTag", "bool", "tag"),
    Method("AddTag", {}, "tag"),
    Method("RemoveTag", "bool", "tag"),
    Method("Destroy"),
    Method("AddComponent", "Component", "typeName"),
    Method("RemoveComponent", "bool", "component or typeName"),
    Method("SetParent", {}, "parent[, keepWorldPose]"),
    Method("CreateChild", "Entity", "[name]"),
    Method("On", "Subscription", "name, callback"),
    Method("Emit", "number", "name, arguments..."),
    Method("GetScript", "any", "[name]"),
};

inline constexpr std::array TransformMembers{
    Property("position", "Vector3", true),
    Property("worldPosition", "Vector3", true),
    Property("scale", "Vector3", true),
    Property("rotation", "Vector3", true),
    Property("worldRotation", "Vector3", true),
    Property("rotation2D", "number", true),
    Property("entity", "Entity"),
    Property("scene", "Scene"),
    Alias("Entity", "Entity"),
    Getter("GetEntity", "entity", "Entity"),
    Getter("GetPosition", "position", "Vector3"),
    Setter("SetPosition", "position"),
    Getter("GetWorldPosition", "worldPosition", "Vector3"),
    Setter("SetWorldPosition", "worldPosition"),
    Getter("GetScale", "scale", "Vector3"),
    Setter("SetScale", "scale"),
    Getter("GetRotation", "rotation", "Vector3"),
    Setter("SetRotation", "rotation"),
    Getter("GetWorldRotation", "worldRotation", "Vector3"),
    Setter("SetWorldRotation", "worldRotation"),
    Getter("GetRotation2D", "rotation2D", "number"),
    Setter("SetRotation2D", "rotation2D"),
    Method("GetScene", "Scene"),
    Method("IsValid", "bool"),
    Method("Translate", {}, "offset"),
};

// What every component reference has, besides the members its type's script bindings describe.
inline constexpr std::array ComponentMembers{
    Property("enabled", "bool", true),
    Property("id", "string"),
    Property("active", "bool"),
    Property("faulted", "bool"),
    Property("entity", "Entity"),
    Property("transform", "Transform"),
    Property("scene", "Scene"),
    Alias("Entity", "Entity"),
    Getter("GetEntity", "entity", "Entity"),
    Getter("GetId", "id", "string"),
    Getter("IsEnabled", "enabled", "bool"),
    Setter("SetEnabled", "enabled"),
    Getter("IsActiveAndEnabled", "active", "bool"),
    Getter("IsFaulted", "faulted", "bool"),
    Method("GetScene", "Scene"),
    Method("IsValid", "bool"),
    Method("StartCoroutine", "number", "function"),
    Method("StopCoroutine", "bool", "id"),
    Method("StopCoroutines"),
    Method("IsCoroutineRunning", "bool", "id"),
    Method("WaitSeconds", "Wait", "seconds"),
    Method("WaitFrames", "Wait", "frames"),
    Method("WaitUntil", "Wait", "condition"),
};

inline constexpr std::array SceneMembers{
    Method("FindEntity", "Entity", "name"),         Method("FindEntityWithTag", "Entity", "tag"),
    Method("CreateEntity", "Entity", "[name]"),     Method("Instantiate", "Entity", "prefabPath[, parent]"),
    Method("DestroyEntity", {}, "entity"),          Method("On", "Subscription", "name, callback"),
    Method("Emit", "number", "name, arguments..."),
};

inline constexpr std::array TimeMembers{
    Property("deltaTime", "number"),
    Property("unscaledDeltaTime", "number"),
    Property("time", "number"),
    Property("unscaledTime", "number"),
    Property("frameCount", "number"),
    Property("fixedDeltaTime", "number"),
    Property("timeScale", "number", true),
    Getter("GetDeltaTime", "deltaTime", "number"),
    Getter("GetUnscaledDeltaTime", "unscaledDeltaTime", "number"),
    Getter("GetTime", "time", "number"),
    Getter("GetUnscaledTime", "unscaledTime", "number"),
    Getter("GetFrameCount", "frameCount", "number"),
    Getter("GetFixedDeltaTime", "fixedDeltaTime", "number"),
    Getter("GetTimeScale", "timeScale", "number"),
    Setter("SetTimeScale", "timeScale"),
};

inline constexpr std::array InputMembers{
    Property("mousePosition", "Vector2"),
    Property("mouseDelta", "Vector2"),
    Property("scrollDelta", "Vector2"),
    Property("gamepadCount", "number"),
    Property("cursorLocked", "bool", true),
    Getter("GetMousePosition", "mousePosition", "Vector2"),
    Getter("GetMouseDelta", "mouseDelta", "Vector2"),
    Getter("GetScrollDelta", "scrollDelta", "Vector2"),
    Getter("GetGamepadCount", "gamepadCount", "number"),
    Getter("IsCursorLocked", "cursorLocked", "bool"),
    Setter("SetCursorLocked", "cursorLocked"),
    Method("IsKeyDown", "bool", "key"),
    Method("IsKeyPressed", "bool", "key"),
    Method("IsKeyReleased", "bool", "key"),
    Method("IsMouseButtonDown", "bool", "button"),
    Method("IsMouseButtonPressed", "bool", "button"),
    Method("IsMouseButtonReleased", "bool", "button"),
    Method("IsGamepadButtonDown", "bool", "button[, gamepad]"),
    Method("IsGamepadButtonPressed", "bool", "button[, gamepad]"),
    Method("IsGamepadButtonReleased", "bool", "button[, gamepad]"),
    Method("GetGamepadAxis", "number", "axis[, gamepad]"),
    Method("IsActionDown", "bool", "action"),
    Method("IsActionPressed", "bool", "action"),
    Method("IsActionReleased", "bool", "action"),
    Method("GetActionValue", "number", "action"),
    Method("GetActionVector2", "Vector2", "action"),
};

inline constexpr std::array PhysicsMembers{
    Method("Raycast", "Hit", "origin, direction, maxDistance[, ignore, layerMask, includeTriggers]"),
    Method("SphereCast", "Hit", "origin, radius, direction, maxDistance[, ignore, layerMask, includeTriggers]"),
    Method("Raycast2D", "Hit2D", "origin, direction, maxDistance[, ignore, layerMask, includeTriggers]"),
    Method("CircleCast2D", "Hit2D", "origin, radius, direction, maxDistance[, ignore, layerMask, includeTriggers]"),
};

inline constexpr std::array HitMembers{Property("entity", "Entity"), Property("collider", "Component"),
                                       Property("point", "Vector3"), Property("normal", "Vector3"),
                                       Property("distance", "number")};
inline constexpr std::array Hit2DMembers{Property("entity", "Entity"), Property("collider", "Component"),
                                         Property("point", "Vector2"), Property("normal", "Vector2"),
                                         Property("distance", "number")};
inline constexpr std::array CollisionMembers{
    Property("other", "Entity"),           Property("normal", "Vector3"),    Property("relativeVelocity", "Vector3"),
    Property("impulse", "number"),         Property("pointCount", "number"), Property("point", "Vector3"),
    Method("GetPoint", "Vector3", "index")};
inline constexpr std::array Collision2DMembers{
    Property("other", "Entity"),           Property("normal", "Vector2"),    Property("relativeVelocity", "Vector2"),
    Property("impulse", "number"),         Property("pointCount", "number"), Property("point", "Vector2"),
    Method("GetPoint", "Vector2", "index")};
inline constexpr std::array TriggerContactMembers{Property("other", "Entity"), Property("isTrigger", "bool")};
inline constexpr std::array SubscriptionMembers{Property("active", "bool"), Method("Cancel", "bool"),
                                                Method("IsActive", "bool")};
inline constexpr std::array Vector2Members{Property("x", "number", true), Property("y", "number", true)};
inline constexpr std::array Vector3Members{Property("x", "number", true), Property("y", "number", true),
                                           Property("z", "number", true)};
inline constexpr std::array ColorMembers{Property("r", "number", true), Property("g", "number", true),
                                         Property("b", "number", true), Property("a", "number", true)};
// What `import spot` gives.
inline constexpr std::array ModuleMembers{Property(Input, Input),     Property(Scene, Scene),
                                          Property(Time, Time),       Property(Physics, Physics),
                                          Property(Entity, "Entity"), Property(Component, Component)};

} // namespace Detail

inline constexpr std::array Types{
    Type{"Entity", Detail::EntityMembers},
    Type{"Transform", Detail::TransformMembers},
    Type{Component, Detail::ComponentMembers},
    Type{Scene, Detail::SceneMembers},
    Type{Time, Detail::TimeMembers},
    Type{Input, Detail::InputMembers},
    Type{Physics, Detail::PhysicsMembers},
    Type{"Hit", Detail::HitMembers},
    Type{"Hit2D", Detail::Hit2DMembers},
    Type{"Collision", Detail::CollisionMembers},
    Type{"Collision2D", Detail::Collision2DMembers},
    Type{"TriggerContact", Detail::TriggerContactMembers},
    Type{"TriggerContact2D", Detail::TriggerContactMembers},
    Type{"Subscription", Detail::SubscriptionMembers},
    Type{"Vector2", Detail::Vector2Members},
    Type{"Vector3", Detail::Vector3Members},
    Type{"Color", Detail::ColorMembers},
    Type{Module, Detail::ModuleMembers},
};

constexpr const Type* FindType(std::string_view name) {
    for (const auto& type : Types) {
        if (type.name == name)
            return &type;
    }
    return nullptr;
}

constexpr const Member* FindMember(std::string_view type, std::string_view name, MemberKind kind) {
    if (const Type* found = FindType(type)) {
        for (const auto& member : found->members) {
            if (member.name == name && member.kind == kind)
                return &member;
        }
    }
    return nullptr;
}

} // namespace Spot::ScriptApi
