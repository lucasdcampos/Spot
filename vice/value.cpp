#include "runtime.h"

#include <vice/vice.h>

#include <charconv>
#include <system_error>

namespace Vice {

std::string_view Object::GetTypeName() const {
    return "object";
}
Value Object::GetIndex(const Value&) const {
    throw std::runtime_error("Object does not support indexing");
}
void Object::SetIndex(const Value&, const Value&) {
    throw std::runtime_error("Object does not support index assignment");
}

bool Object::HasProperty(std::string_view) const {
    return false;
}

bool Object::HasMethod(std::string_view) const {
    return false;
}

Value Object::Get(std::string_view name) const {
    throw std::runtime_error("Unknown property '" + std::string(name) + "'");
}

void Object::Set(std::string_view name, const Value&) {
    throw std::runtime_error("Unknown or read-only property '" + std::string(name) + "'");
}

Value Object::Call(std::string_view name, std::span<const Value>) {
    throw std::runtime_error("Unknown method '" + std::string(name) + "'");
}

std::vector<std::string> Object::GetFieldNames() const {
    return {};
}

bool Object::IsCallable() const {
    return false;
}

Value Object::Invoke(std::span<const Value>) {
    throw std::runtime_error("Value is not callable");
}

std::string ToString(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* number = std::get_if<double>(&value)) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *number);
        if (result.ec != std::errc()) {
            throw std::runtime_error("Could not format number");
        }
        return std::string(buffer, result.ptr);
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* vector = std::get_if<Vector2>(&value))
        return "Vector2(" + ToString(vector->x) + ", " + ToString(vector->y) + ")";
    if (const auto* vector = std::get_if<Vector3>(&value))
        return "Vector3(" + ToString(vector->x) + ", " + ToString(vector->y) + ", " + ToString(vector->z) + ")";
    if (const auto* color = std::get_if<Color>(&value))
        return "Color(" + ToString(color->r) + ", " + ToString(color->g) + ", " + ToString(color->b) + ", " +
               ToString(color->a) + ")";
    if (const auto* object = std::get_if<ObjectRef>(&value)) {
        if (const auto* member = dynamic_cast<const Detail::EnumValue*>(object->get()))
            return member->GetQualifiedName();
        return *object ? "<" + std::string((*object)->GetTypeName()) + ">" : "null";
    }
    return "null";
}

Error::Error(std::string_view sourceName, SourceLocation location, std::string_view message)
    : std::runtime_error(std::string(sourceName) + ":" + std::to_string(location.line) + ":" +
                         std::to_string(location.column) + ": " + std::string(message)),
      m_location(location) {
}

Error::Error(Traced, std::string message, SourceLocation location, std::string trace)
    : std::runtime_error(std::move(message)), m_location(location), m_trace(std::move(trace)) {
}

Error Error::WithTrace(std::string trace) const {
    auto message = std::string(what()) + "\n" + trace;
    return Error(Traced{}, std::move(message), m_location, std::move(trace));
}

} // namespace Vice
