#pragma once

#include <vice/vice.h>

#include <utility>

namespace Vice::Detail {

class ManagedObject : public Object {
    friend void DestroyManaged(ManagedObject* object) noexcept;
    ManagedObject* m_nextDeletion = nullptr;
};
void DestroyManaged(ManagedObject* object) noexcept;
template <typename T, typename... Args>
std::shared_ptr<T> MakeManaged(Args&&... args) {
    return {new T(std::forward<Args>(args)...), [](T* object) { DestroyManaged(object); }};
}

// Callables compare by what they invoke, so two reads of the same bound method are equal.
class CallableObject : public Object {
public:
    std::string_view GetTypeName() const override { return "function"; }
    bool IsCallable() const override { return true; }
    virtual bool IsSameCallable(const CallableObject& other) const = 0;
};

// A method read as a value. It dispatches through the receiver on each call, preserving virtual lookup.
class BoundMethod final : public CallableObject {
public:
    BoundMethod(ObjectRef receiver, std::string name) : m_receiver(std::move(receiver)), m_name(std::move(name)) {}
    Value Invoke(std::span<const Value> arguments) override { return m_receiver->Call(m_name, arguments); }
    bool IsSameCallable(const CallableObject& other) const override;
    const ObjectRef& GetReceiver() const { return m_receiver; }

private:
    ObjectRef m_receiver;
    std::string m_name;
};

// A member of a script enum. Each member is one object, so == compares identity; `name` and `value` (its position,
// from 0) are read-only.
class EnumValue final : public Object {
public:
    EnumValue(std::string type, std::string name, double value)
        : m_type(std::move(type)), m_name(std::move(name)), m_value(value) {}
    std::string_view GetTypeName() const override { return "enum"; }
    bool HasProperty(std::string_view name) const override { return name == "name" || name == "value"; }
    Value Get(std::string_view name) const override {
        if (name == "name")
            return m_name;
        if (name == "value")
            return m_value;
        return Object::Get(name);
    }
    std::string GetQualifiedName() const { return m_type + "." + m_name; }

private:
    std::string m_type;
    std::string m_name;
    double m_value;
};

// Runtime semantics shared by evaluators. No syntax nodes, source tokens or engine types cross this boundary.
enum class Operation {
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};
double Number(const Value& value);
bool Boolean(const Value& value);
Value Binary(Operation operation, const Value& left, const Value& right);
Value Negate(const Value& value);
bool IsStruct(const Value& value);
Value GetMember(const Value& receiver, std::string_view name);
Value GetIndex(const Value& receiver, const Value& index);
void SetIndex(const Value& receiver, const Value& index, const Value& value);
// Returns a replacement value for structs; host objects are changed through their setter instead.
Value SetMember(Value receiver, std::string_view name, const Value& value);
Value CallMember(const Value& receiver, std::string_view name, std::span<const Value> arguments);
Value Construct(std::string_view name, std::span<const Value> arguments);
bool IsConstructor(std::string_view name);

} // namespace Vice::Detail
