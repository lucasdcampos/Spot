#include "runtime.h"
#include "collections.h"

#include <vice/language.h>

#include <cmath>
#include <optional>
#include <type_traits>

namespace Vice::Detail {

void DestroyManaged(ManagedObject* object) noexcept {
    // Loops can build deep graphs: queue nested releases instead of recursing through shared_ptr destructors.
    thread_local ManagedObject* pending = nullptr;
    thread_local bool draining = false;
    object->m_nextDeletion = pending;
    pending = object;
    if (draining)
        return;
    draining = true;
    while (pending) {
        auto* next = pending;
        pending = next->m_nextDeletion;
        delete next;
    }
    draining = false;
}

double Number(const Value& value) {
    const auto* number = std::get_if<double>(&value);
    if (!number || !std::isfinite(*number))
        throw std::runtime_error("Expected a finite number");
    return *number;
}

namespace {

template <typename T>
double* Field(T& value, std::string_view name) {
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

Object& Host(const ObjectRef& object) {
    if (!object)
        throw std::runtime_error("Cannot access a null object");
    return *object;
}

double Finite(double value) {
    if (!std::isfinite(value))
        throw std::runtime_error("Numeric result is not finite");
    return value;
}

double Arithmetic(Operation operation, double a, double b) {
    switch (operation) {
    case Operation::Add:
        return Finite(a + b);
    case Operation::Subtract:
        return Finite(a - b);
    case Operation::Multiply:
        return Finite(a * b);
    case Operation::Divide:
        if (b == 0)
            throw std::runtime_error("Division by zero");
        return Finite(a / b);
    case Operation::Modulo:
        // Floored, so the result takes the divisor's sign: -1 % 4 is 3, which wraps indices and angles.
        if (b == 0)
            throw std::runtime_error("Modulo by zero");
        return Finite(a - b * std::floor(a / b));
    default:
        throw std::runtime_error("Unknown operation");
    }
}

// Applies an operation to each component, pairing structs of one type or a struct with a number.
template <typename T, typename F>
T Componentwise(const T& value, F&& function) {
    if constexpr (std::is_same_v<T, Vector2>)
        return {function(value.x, 0), function(value.y, 1)};
    else if constexpr (std::is_same_v<T, Vector3>)
        return {function(value.x, 0), function(value.y, 1), function(value.z, 2)};
    else
        return {function(value.r, 0), function(value.g, 1), function(value.b, 2), function(value.a, 3)};
}
template <typename T>
double Component(const T& value, int index) {
    if constexpr (std::is_same_v<T, Vector2>)
        return index == 0 ? value.x : value.y;
    else if constexpr (std::is_same_v<T, Vector3>)
        return index == 0 ? value.x : index == 1 ? value.y : value.z;
    else
        return index == 0 ? value.r : index == 1 ? value.g : index == 2 ? value.b : value.a;
}

template <typename T>
std::optional<Value> StructArithmetic(Operation operation, const Value& left, const Value& right) {
    const auto* a = std::get_if<T>(&left);
    const auto* b = std::get_if<T>(&right);
    const auto* leftNumber = std::get_if<double>(&left);
    const auto* rightNumber = std::get_if<double>(&right);
    const bool componentwise = operation == Operation::Add || operation == Operation::Subtract ||
                               operation == Operation::Multiply || operation == Operation::Divide;
    if (!componentwise || (!a && !b))
        return std::nullopt;
    if (a && b)
        return Componentwise(*a, [&](double x, int i) { return Arithmetic(operation, x, Component(*b, i)); });
    // Scaling: vector * number, number * vector and vector / number.
    if (a && rightNumber && (operation == Operation::Multiply || operation == Operation::Divide))
        return Componentwise(*a, [&](double x, int) { return Arithmetic(operation, x, Number(right)); });
    if (b && leftNumber && operation == Operation::Multiply)
        return Componentwise(*b, [&](double x, int) { return Arithmetic(operation, Number(left), x); });
    throw std::runtime_error("Operation is not defined between these values");
}

double Dot(const Vector2& a, const Vector2& b) {
    return a.x * b.x + a.y * b.y;
}
double Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

template <typename T>
Value VectorMethod(const T& vector, std::string_view name, std::span<const Value> arguments) {
    const auto other = [&]() -> const T& {
        if (arguments.size() != 1)
            throw std::runtime_error("Incorrect argument count");
        const auto* value = std::get_if<T>(&arguments[0]);
        if (!value)
            throw std::runtime_error("Expected a vector of the same type");
        return *value;
    };
    const auto none = [&] {
        if (!arguments.empty())
            throw std::runtime_error("Incorrect argument count");
    };
    if (name == "length") {
        none();
        return Finite(std::sqrt(Dot(vector, vector)));
    }
    if (name == "length_squared") {
        none();
        return Finite(Dot(vector, vector));
    }
    if (name == "normalized") {
        none();
        const double length = std::sqrt(Dot(vector, vector));
        // The zero vector has no direction and stays zero.
        return length == 0 ? vector : Componentwise(vector, [&](double x, int) { return x / length; });
    }
    if (name == "dot")
        return Finite(Dot(vector, other()));
    if (name == "distance") {
        const auto& target = other();
        const auto difference = Componentwise(vector, [&](double x, int i) { return x - Component(target, i); });
        return Finite(std::sqrt(Dot(difference, difference)));
    }
    if constexpr (std::is_same_v<T, Vector3>) {
        if (name == "cross") {
            const auto& b = other();
            return Vector3{Finite(vector.y * b.z - vector.z * b.y), Finite(vector.z * b.x - vector.x * b.z),
                           Finite(vector.x * b.y - vector.y * b.x)};
        }
    }
    throw std::runtime_error("Unknown method '" + std::string(name) + "'");
}

} // namespace

bool IsStruct(const Value& value) {
    return std::holds_alternative<Vector2>(value) || std::holds_alternative<Vector3>(value) ||
           std::holds_alternative<Color>(value);
}

Value Negate(const Value& value) {
    if (IsStruct(value))
        return Binary(Operation::Multiply, value, -1.0);
    return Binary(Operation::Subtract, 0.0, value);
}

bool Boolean(const Value& value) {
    if (const auto* boolean = std::get_if<bool>(&value))
        return *boolean;
    throw std::runtime_error("Expected a boolean");
}

Value Binary(Operation operation, const Value& left, const Value& right) {
    if (operation == Operation::Equal || operation == Operation::NotEqual) {
        const auto isNull = [](const Value& value) {
            const auto* object = std::get_if<ObjectRef>(&value);
            return std::holds_alternative<std::monostate>(value) || (object && !*object);
        };
        bool equal = left == right || (isNull(left) && isNull(right));
        const auto* first = std::get_if<ObjectRef>(&left);
        const auto* second = std::get_if<ObjectRef>(&right);
        if (!equal && first && second) {
            const auto* a = dynamic_cast<const CallableObject*>(first->get());
            const auto* b = dynamic_cast<const CallableObject*>(second->get());
            equal = a && b && a->IsSameCallable(*b);
        }
        return operation == Operation::Equal ? equal : !equal;
    }
    if (operation == Operation::Add && std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::string>(right))
        return std::get<std::string>(left) + std::get<std::string>(right);
    if (auto result = StructArithmetic<Vector2>(operation, left, right))
        return *result;
    if (auto result = StructArithmetic<Vector3>(operation, left, right))
        return *result;
    if (auto result = StructArithmetic<Color>(operation, left, right))
        return *result;
    const double a = Number(left), b = Number(right);
    switch (operation) {
    case Operation::Less:
        return a < b;
    case Operation::LessEqual:
        return a <= b;
    case Operation::Greater:
        return a > b;
    case Operation::GreaterEqual:
        return a >= b;
    default:
        return Arithmetic(operation, a, b);
    }
}

bool BoundMethod::IsSameCallable(const CallableObject& other) const {
    const auto* method = dynamic_cast<const BoundMethod*>(&other);
    return method && method->m_receiver == m_receiver && method->m_name == m_name;
}

Value GetMember(const Value& receiver, std::string_view name) {
    return std::visit(
        [name](auto value) -> Value {
            using T = decltype(value);
            if constexpr (std::is_same_v<T, ObjectRef>) {
                auto& object = Host(value);
                if (object.HasMethod(name) && !object.HasProperty(name))
                    return ObjectRef(std::make_shared<BoundMethod>(value, std::string(name)));
                return object.Get(name);
            } else {
                if (const auto* field = Field(value, name))
                    return *field;
                throw std::runtime_error("Unknown property '" + std::string(name) + "'");
            }
        },
        receiver);
}

Value SetMember(Value receiver, std::string_view name, const Value& value) {
    std::visit(
        [&](auto& target) {
            using T = std::remove_cvref_t<decltype(target)>;
            if constexpr (std::is_same_v<T, ObjectRef>) {
                Host(target).Set(name, value);
            } else {
                if (auto* field = Field(target, name)) {
                    *field = Number(value);
                    return;
                }
                throw std::runtime_error("Unknown or read-only property '" + std::string(name) + "'");
            }
        },
        receiver);
    return receiver;
}

Value CallMember(const Value& receiver, std::string_view name, std::span<const Value> arguments) {
    if (const auto* object = std::get_if<ObjectRef>(&receiver))
        return Host(*object).Call(name, arguments);
    if (const auto* vector = std::get_if<Vector2>(&receiver))
        return VectorMethod(*vector, name, arguments);
    if (const auto* vector = std::get_if<Vector3>(&receiver))
        return VectorMethod(*vector, name, arguments);
    throw std::runtime_error("Methods require an object or a vector");
}

Value GetIndex(const Value& receiver, const Value& index) {
    if (const auto* object = std::get_if<ObjectRef>(&receiver))
        return Host(*object).GetIndex(index);
    throw std::runtime_error("Indexing requires an object");
}

void SetIndex(const Value& receiver, const Value& index, const Value& value) {
    if (const auto* object = std::get_if<ObjectRef>(&receiver)) {
        Host(*object).SetIndex(index, value);
        return;
    }
    throw std::runtime_error("Index assignment requires an object");
}

bool IsConstructor(std::string_view name) {
    return IsValueConstructor(name);
}

Value Construct(std::string_view name, std::span<const Value> arguments) {
    if (name == "range")
        return MakeRange(arguments);
    const std::size_t count = name == "Vector2" ? 2 : name == "Vector3" ? 3 : 4;
    if (arguments.size() != count)
        throw std::runtime_error(std::string(name) + " expects " + std::to_string(count) + " arguments");
    if (name == "Vector2")
        return Vector2{Number(arguments[0]), Number(arguments[1])};
    if (name == "Vector3")
        return Vector3{Number(arguments[0]), Number(arguments[1]), Number(arguments[2])};
    return Color{Number(arguments[0]), Number(arguments[1]), Number(arguments[2]), Number(arguments[3])};
}

} // namespace Vice::Detail
