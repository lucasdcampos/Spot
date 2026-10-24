#pragma once

#include "spot/core/math/color.h"
#include "spot/core/math/vector.h"
#include "spot/scene/component.h"

#include <cmath>
#include <concepts>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

namespace Spot {

// Shared values and host objects have no dependency on a language runtime.
class ScriptObject;
using ScriptObjectRef = std::shared_ptr<ScriptObject>;
using ScriptValue = std::variant<std::monostate, bool, double, std::string, Vector2, Vector3, Color, ScriptObjectRef>;

// Runtime adapters forward their host-object operations here; objects retain handles rather than engine pointers.
class ScriptObject {
public:
    virtual ~ScriptObject() = default;
    virtual ScriptValue Get(std::string_view name) const {
        throw std::runtime_error("Unknown script property '" + std::string(name) + "'");
    }
    virtual void Set(std::string_view name, const ScriptValue&) {
        throw std::runtime_error("Cannot write script property '" + std::string(name) + "'");
    }
    virtual ScriptValue Call(std::string_view name, std::span<const ScriptValue>) {
        throw std::runtime_error("Unknown script method '" + std::string(name) + "'");
    }
    // Runtime adapters may cache a property token; zero keeps dynamic host objects on their named path.
    virtual std::size_t ResolveProperty(std::string_view) const { return 0; }
    virtual ScriptValue GetResolved(std::size_t, std::string_view name) const { return Get(name); }
    virtual void SetResolved(std::size_t, std::string_view name, const ScriptValue& value) { Set(name, value); }
    virtual bool HasMethod(std::string_view) const { return true; }
    // Discovery must not invoke getters or mutate the object.
    virtual bool HasProperty(std::string_view) const { return false; }
    // Functions of a script language, such as callbacks given to events. Invoking runs the function in its own
    // runtime and component scope, and returns its result.
    virtual bool IsCallable() const { return false; }
    virtual ScriptValue Invoke(std::span<const ScriptValue>) {
        throw std::runtime_error("Script value is not callable");
    }
    // Context-backed references can retain their runtime wrappers until that runtime closes.
    virtual bool IsPersistentReference() const { return false; }
};

namespace Detail {

struct BuiltinScriptBindingsTag {};
void EnsureBuiltinScriptBindings();

template <typename T>
ScriptValue ToScriptValue(const T& value) {
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        const double number = static_cast<double>(value);
        if (!std::isfinite(number))
            throw std::runtime_error("Number is outside the script value range");
        if constexpr (std::is_integral_v<T>) {
            if (std::abs(number) > 9007199254740991.0)
                throw std::runtime_error("Integer is outside the exact script value range");
        }
        return number;
    } else {
        return ScriptValue(value);
    }
}

template <typename T>
T FromScriptValue(const ScriptValue& value) {
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        const auto* number = std::get_if<double>(&value);
        if (!number || !std::isfinite(*number))
            throw std::runtime_error("Expected a finite number");
        if constexpr (std::is_integral_v<T>) {
            if (std::trunc(*number) != *number || std::abs(*number) > 9007199254740991.0)
                throw std::runtime_error("Expected an exactly representable integer");
        }
        if (static_cast<long double>(*number) < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
            static_cast<long double>(*number) > static_cast<long double>(std::numeric_limits<T>::max()))
            throw std::runtime_error("Number is out of range");
        return static_cast<T>(*number);
    } else {
        const auto* typed = std::get_if<T>(&value);
        if (!typed)
            throw std::runtime_error("Incorrect script value type");
        if constexpr (std::is_same_v<T, Vector2>) {
            if (!std::isfinite(typed->x) || !std::isfinite(typed->y))
                throw std::runtime_error("Expected finite vector coordinates");
        } else if constexpr (std::is_same_v<T, Vector3>) {
            if (!std::isfinite(typed->x) || !std::isfinite(typed->y) || !std::isfinite(typed->z))
                throw std::runtime_error("Expected finite vector coordinates");
        } else if constexpr (std::is_same_v<T, Color>) {
            if (!std::isfinite(typed->r) || !std::isfinite(typed->g) || !std::isfinite(typed->b) ||
                !std::isfinite(typed->a))
                throw std::runtime_error("Expected finite color channels");
        }
        return *typed;
    }
}

// Shared struct constructors, field accessors and string formatting for language runtime adapters.
ScriptValue ConstructScriptStruct(std::string_view name, std::span<const ScriptValue> arguments);
ScriptValue GetScriptStructField(const ScriptValue& value, std::string_view name);
void SetScriptStructField(ScriptValue& value, std::string_view name, const ScriptValue& fieldValue);
std::string FormatScriptValue(const ScriptValue& value, std::string_view nullLiteral = "null");

// The name ScriptApi gives a value type, as tools show it: "number", "Vector3", or "any" for host references.
template <typename T>
constexpr std::string_view GetScriptValueTypeName() {
    if constexpr (std::is_void_v<T>)
        return {};
    else if constexpr (std::is_same_v<T, bool>)
        return "bool";
    else if constexpr (std::is_arithmetic_v<T>)
        return "number";
    else if constexpr (std::is_same_v<T, std::string>)
        return "string";
    else if constexpr (std::is_same_v<T, Vector2>)
        return "Vector2";
    else if constexpr (std::is_same_v<T, Vector3>)
        return "Vector3";
    else if constexpr (std::is_same_v<T, Color>)
        return "Color";
    else
        return "any";
}

struct ScriptProperty {
    std::function<ScriptValue(const Component&)> get;
    std::function<void(Component&, const ScriptValue&)> set;
    // What it holds, as GetScriptValueTypeName() names it.
    std::string type = "any";
};
using ScriptMethod = std::function<ScriptValue(Component&, std::span<const ScriptValue>)>;

void SetScriptProperty(std::type_index type, std::string name, ScriptProperty property);
// The method returns `returnType`, as GetScriptValueTypeName() names it; empty when it returns nothing.
void SetScriptMethod(std::type_index type, std::string name, ScriptMethod method, std::string returnType = "any");
// The type of a property the bindings of `type` describe; "any" when they do not.
std::string GetScriptPropertyType(std::type_index type, std::string_view name);
void RemoveScriptBindings(std::type_index type);
// Tokens retain no callbacks: replacement keeps the token, unregistration invalidates it.
std::size_t ResolveScriptProperty(const Component& component, std::string_view name);
std::shared_ptr<const ScriptProperty> FindResolvedScriptProperty(std::size_t id);
ScriptValue GetScriptProperty(const Component& component, std::string_view name);
void WriteScriptProperty(Component& component, std::string_view name, const ScriptValue& value);
ScriptValue CallScriptMethod(Component& component, std::string_view name, std::span<const ScriptValue> arguments);
bool HasScriptMethod(const Component& component, std::string_view name);
bool HasScriptProperty(const Component& component, std::string_view name);

template <typename T, typename R, typename... Args, typename Method, std::size_t... I>
ScriptValue InvokeScriptMethod(T& component, Method method, std::span<const ScriptValue> arguments,
                               std::index_sequence<I...>) {
    if (arguments.size() != sizeof...(Args))
        throw std::runtime_error("Incorrect method argument count");
    if constexpr (std::is_void_v<R>) {
        std::invoke(method, component, FromScriptValue<std::remove_cvref_t<Args>>(arguments[I])...);
        return {};
    } else {
        return ToScriptValue(
            std::invoke(method, component, FromScriptValue<std::remove_cvref_t<Args>>(arguments[I])...));
    }
}

} // namespace Detail

// Register the component normally, then describe its script API once, before running scenes. Adding bindings
// replaces members with the same name. UnregisterComponent() also discards callbacks before a game module unloads.
template <std::derived_from<Component> T>
class ScriptBindings {
public:
    ScriptBindings() { Detail::EnsureBuiltinScriptBindings(); }
    explicit ScriptBindings(Detail::BuiltinScriptBindingsTag) {}

    template <typename U>
    ScriptBindings& Field(std::string name, U T::* member) {
        return Property(
            std::move(name), [member](const T& component) { return component.*member; },
            [member](T& component, U value) { component.*member = std::move(value); });
    }

    template <typename Getter, typename Setter>
    ScriptBindings& Property(std::string name, Getter getter, Setter setter) {
        using U = std::remove_cvref_t<std::invoke_result_t<Getter, const T&>>;
        Detail::SetScriptProperty(
            typeid(T), std::move(name),
            {[getter](const Component& component) {
                 return Detail::ToScriptValue(std::invoke(getter, dynamic_cast<const T&>(component)));
             },
             [setter](Component& component, const ScriptValue& value) {
                 std::invoke(setter, dynamic_cast<T&>(component), Detail::FromScriptValue<U>(value));
             },
             std::string(Detail::GetScriptValueTypeName<U>())});
        return *this;
    }

    template <typename Getter>
    ScriptBindings& ReadOnly(std::string name, Getter getter) {
        using U = std::remove_cvref_t<std::invoke_result_t<Getter, const T&>>;
        Detail::SetScriptProperty(typeid(T), std::move(name),
                                  {[getter](const Component& component) {
                                       return Detail::ToScriptValue(
                                           std::invoke(getter, dynamic_cast<const T&>(component)));
                                   },
                                   {},
                                   std::string(Detail::GetScriptValueTypeName<U>())});
        return *this;
    }

    template <typename R, typename... Args>
    ScriptBindings& Method(std::string name, R (T::*method)(Args...)) {
        Detail::SetScriptMethod(
            typeid(T), std::move(name),
            [method](Component& component, std::span<const ScriptValue> arguments) {
                return Detail::InvokeScriptMethod<T, R, Args...>(dynamic_cast<T&>(component), method, arguments,
                                                                 std::index_sequence_for<Args...>{});
            },
            std::string(Detail::GetScriptValueTypeName<std::remove_cvref_t<R>>()));
        return *this;
    }

    template <typename R, typename... Args>
    ScriptBindings& Method(std::string name, R (T::*method)(Args...) const) {
        Detail::SetScriptMethod(
            typeid(T), std::move(name),
            [method](Component& component, std::span<const ScriptValue> arguments) {
                return Detail::InvokeScriptMethod<T, R, Args...>(dynamic_cast<T&>(component), method, arguments,
                                                                 std::index_sequence_for<Args...>{});
            },
            std::string(Detail::GetScriptValueTypeName<std::remove_cvref_t<R>>()));
        return *this;
    }
};

// A member a component type's script bindings describe, for tools such as the code editor's suggestions.
struct ScriptMemberInfo {
    std::string name;
    bool method = false;
    // What the property holds or the method returns, as ScriptApi names types; empty for methods returning nothing.
    std::string type;
    bool writable = false;
};
// The members the script bindings of the registered component type describe, besides those every component has
// (ScriptApi::Component), sorted by name; none for unknown types.
std::vector<ScriptMemberInfo> GetComponentScriptMembers(std::string_view componentTypeName);

} // namespace Spot
