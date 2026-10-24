#include "spot/scene/scripting/script_bindings.h"

#include "spot/scene/component_registry.h"

#include <algorithm>
#include <charconv>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace Spot::Detail {
namespace {

struct Method {
    ScriptMethod callback;
    std::string returnType;
};
struct Members {
    std::unordered_map<std::string, ScriptProperty> properties;
    std::unordered_map<std::string, Method> methods;
};
struct Registry {
    std::mutex mutex;
    std::unordered_map<std::type_index, Members> types;
};
Registry& Bindings() {
    static Registry registry;
    static const bool cleanupInstalled = [] {
        SetComponentTypeCleanup(RemoveScriptBindings);
        return true;
    }();
    static_cast<void>(cleanupInstalled);
    return registry;
}
void CheckName(std::string_view name) {
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (name.empty() || !letter(name.front()))
        throw std::invalid_argument("Script member names must be identifiers");
    for (const char c : name) {
        if (!letter(c) && !(c >= '0' && c <= '9'))
            throw std::invalid_argument("Script member names must be identifiers");
    }
    if (name == "GetEntity" || name == "GetScene" || name == "GetId" || name == "IsEnabled" || name == "SetEnabled" ||
        name == "IsActiveAndEnabled" || name == "IsFaulted" || name == "IsValid" || name == "Entity" ||
        name == "entity" || name == "scene" || name == "transform")
        throw std::invalid_argument("Script member name is reserved by Component");
    if (name == "enabled" || name == "active" || name == "faulted" || name == "id" || name == "IsValid")
        throw std::invalid_argument("Script member name is reserved by Component");
}
ScriptProperty Property(const Component& component, std::string_view name) {
    auto& registry = Bindings();
    const std::lock_guard lock(registry.mutex);
    const auto type = registry.types.find(typeid(component));
    if (type != registry.types.end()) {
        const auto property = type->second.properties.find(std::string(name));
        if (property != type->second.properties.end())
            return property->second;
    }
    throw std::runtime_error("Unknown script property '" + std::string(name) + "'");
}

template <typename T>
float* StructField(T& value, std::string_view name) {
    if constexpr (std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3>) {
        if (name == "x")
            return &value.x;
        if (name == "y")
            return &value.y;
        if constexpr (std::is_same_v<T, Vector3>) {
            if (name == "z")
                return &value.z;
        }
    } else if constexpr (std::is_same_v<T, Color>) {
        if (name == "r")
            return &value.r;
        if (name == "g")
            return &value.g;
        if (name == "b")
            return &value.b;
        if (name == "a")
            return &value.a;
    }
    return nullptr;
}

std::string FormatNumber(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc())
        throw std::runtime_error("Could not format number");
    return std::string(buffer, result.ptr);
}

} // namespace

ScriptValue ConstructScriptStruct(std::string_view name, std::span<const ScriptValue> arguments) {
    const std::size_t count = name == "Vector2" ? 2 : name == "Vector3" ? 3 : name == "Color" ? 4 : 0;
    if (count == 0)
        throw std::runtime_error("Unknown script value constructor '" + std::string(name) + "'");
    if (arguments.size() != count)
        throw std::runtime_error(std::string(name) + " expects " + std::to_string(count) + " arguments");
    if (name == "Vector2")
        return Vector2{FromScriptValue<float>(arguments[0]), FromScriptValue<float>(arguments[1])};
    if (name == "Vector3")
        return Vector3{FromScriptValue<float>(arguments[0]), FromScriptValue<float>(arguments[1]),
                       FromScriptValue<float>(arguments[2])};
    return Color{FromScriptValue<float>(arguments[0]), FromScriptValue<float>(arguments[1]),
                 FromScriptValue<float>(arguments[2]), FromScriptValue<float>(arguments[3])};
}

ScriptValue GetScriptStructField(const ScriptValue& value, std::string_view name) {
    return std::visit(
        [name](auto item) -> ScriptValue {
            if (const auto* field = StructField(item, name))
                return static_cast<double>(*field);
            throw std::runtime_error("Unknown property '" + std::string(name) + "'");
        },
        value);
}

void SetScriptStructField(ScriptValue& value, std::string_view name, const ScriptValue& fieldValue) {
    std::visit(
        [&](auto& item) {
            if (auto* field = StructField(item, name)) {
                *field = FromScriptValue<float>(fieldValue);
                return;
            }
            throw std::runtime_error("Unknown or read-only property '" + std::string(name) + "'");
        },
        value);
}

std::string FormatScriptValue(const ScriptValue& value, std::string_view nullLiteral) {
    if (const auto* text = std::get_if<std::string>(&value))
        return *text;
    if (const auto* number = std::get_if<double>(&value))
        return FormatNumber(*number);
    if (const auto* boolean = std::get_if<bool>(&value))
        return *boolean ? "true" : "false";
    if (const auto* vector = std::get_if<Vector2>(&value))
        return "Vector2(" + FormatNumber(vector->x) + ", " + FormatNumber(vector->y) + ")";
    if (const auto* vector = std::get_if<Vector3>(&value))
        return "Vector3(" + FormatNumber(vector->x) + ", " + FormatNumber(vector->y) + ", " + FormatNumber(vector->z) +
               ")";
    if (const auto* color = std::get_if<Color>(&value))
        return "Color(" + FormatNumber(color->r) + ", " + FormatNumber(color->g) + ", " + FormatNumber(color->b) +
               ", " + FormatNumber(color->a) + ")";
    if (const auto* object = std::get_if<ScriptObjectRef>(&value))
        return *object ? "<object>" : std::string(nullLiteral);
    return std::string(nullLiteral);
}

void SetScriptProperty(std::type_index type, std::string name, ScriptProperty property) {
    CheckName(name);
    if (!property.get)
        throw std::invalid_argument("A script property needs a getter");
    auto& registry = Bindings();
    const std::lock_guard lock(registry.mutex);
    auto& members = registry.types[type];
    if (members.methods.contains(name))
        throw std::invalid_argument("Script member is already a method");
    members.properties.insert_or_assign(std::move(name), std::move(property));
}
void SetScriptMethod(std::type_index type, std::string name, ScriptMethod method, std::string returnType) {
    CheckName(name);
    if (!method)
        throw std::invalid_argument("A script method needs a callback");
    auto& registry = Bindings();
    const std::lock_guard lock(registry.mutex);
    auto& members = registry.types[type];
    if (members.properties.contains(name))
        throw std::invalid_argument("Script member is already a property");
    members.methods.insert_or_assign(std::move(name), Method{std::move(method), std::move(returnType)});
}
std::string GetScriptPropertyType(std::type_index type, std::string_view name) {
    auto& registry = Bindings();
    const std::lock_guard lock(registry.mutex);
    const auto members = registry.types.find(type);
    if (members == registry.types.end())
        return "any";
    const auto property = members->second.properties.find(std::string(name));
    return property == members->second.properties.end() ? "any" : property->second.type;
}
void RemoveScriptBindings(std::type_index type) {
    auto& registry = Bindings();
    const std::lock_guard lock(registry.mutex);
    registry.types.erase(type);
}
ScriptValue GetScriptProperty(const Component& component, std::string_view name) {
    return Property(component, name).get(component);
}
void WriteScriptProperty(Component& component, std::string_view name, const ScriptValue& value) {
    const auto property = Property(component, name);
    if (!property.set)
        throw std::runtime_error("Read-only script property '" + std::string(name) + "'");
    property.set(component, value);
}
ScriptValue CallScriptMethod(Component& component, std::string_view name, std::span<const ScriptValue> arguments) {
    ScriptMethod callback;
    {
        auto& registry = Bindings();
        const std::lock_guard lock(registry.mutex);
        const auto type = registry.types.find(typeid(component));
        if (type != registry.types.end()) {
            const auto method = type->second.methods.find(std::string(name));
            if (method != type->second.methods.end())
                callback = method->second.callback;
        }
    }
    if (!callback)
        throw std::runtime_error("Unknown script method '" + std::string(name) + "'");
    return callback(component, arguments);
}
bool HasScriptProperty(const Component& component, std::string_view name) {
    auto& registry = Bindings();
    const std::lock_guard lock(registry.mutex);
    const auto type = registry.types.find(typeid(component));
    return type != registry.types.end() && type->second.properties.contains(std::string(name));
}
bool HasScriptMethod(const Component& component, std::string_view name) {
    auto& registry = Bindings();
    const std::lock_guard lock(registry.mutex);
    const auto type = registry.types.find(typeid(component));
    return type != registry.types.end() && type->second.methods.contains(std::string(name));
}

} // namespace Spot::Detail

namespace Spot {

std::vector<ScriptMemberInfo> GetComponentScriptMembers(std::string_view componentTypeName) {
    Detail::EnsureBuiltinScriptBindings();
    std::vector<ScriptMemberInfo> found;
    auto& registry = Detail::Bindings();
    const std::lock_guard lock(registry.mutex);
    for (const auto& [type, members] : registry.types) {
        const std::string* name = Detail::FindComponentTypeName(type);
        if (!name || *name != componentTypeName)
            continue;
        for (const auto& [member, property] : members.properties)
            found.push_back({.name = member, .type = property.type, .writable = static_cast<bool>(property.set)});
        for (const auto& [member, method] : members.methods)
            found.push_back({.name = member, .method = true, .type = method.returnType});
    }
    std::ranges::sort(found, {}, &ScriptMemberInfo::name);
    return found;
}

} // namespace Spot
