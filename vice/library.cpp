#include <vice/language.h>
#include <vice/vice.h>

#include <utility>

namespace Vice {

void Library::CheckName(std::string_view name) const {
    const auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (name.empty() || !start(name.front()) || FindKeyword(name))
        throw std::invalid_argument("Invalid library member name");
    for (char c : name) {
        if (!start(c) && !(c >= '0' && c <= '9'))
            throw std::invalid_argument("Invalid library member name");
    }
    if (m_constants.contains(std::string(name)) || m_functions.contains(std::string(name)))
        throw std::invalid_argument("Library member is already declared");
}
bool Library::HasProperty(std::string_view name) const {
    return m_constants.contains(std::string(name));
}
bool Library::HasMethod(std::string_view name) const {
    return m_functions.contains(std::string(name));
}

Library& Library::Constant(std::string name, Value value) {
    CheckName(name);
    m_constants.emplace(std::move(name), std::move(value));
    return *this;
}
Library& Library::Function(std::string name, NativeFunction function) {
    CheckName(name);
    if (!function)
        throw std::invalid_argument("A library function needs a callback");
    m_functions.emplace(std::move(name), std::move(function));
    return *this;
}
Value Library::Get(std::string_view name) const {
    if (const auto found = m_constants.find(std::string(name)); found != m_constants.end())
        return found->second;
    return Object::Get(name);
}
Value Library::Call(std::string_view name, std::span<const Value> arguments) {
    if (const auto found = m_functions.find(std::string(name)); found != m_functions.end())
        return found->second(arguments);
    return Object::Call(name, arguments);
}

} // namespace Vice
