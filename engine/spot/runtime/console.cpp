#include "spot/runtime/console.h"

#include "spot/core/io/archive.h"

#include <algorithm>
#include <exception>
#include <map>
#include <stdexcept>
#include <utility>

namespace Spot {
namespace Detail {
struct ConsoleState {
    struct Entry {
        ConsoleCommand command;
        std::uint64_t id;
        std::string defaultValue;
        std::function<std::string()> get;
        std::function<bool(std::string_view)> set;
    };
    std::map<std::string, Entry, std::less<>> entries;
    std::uint64_t nextId = 1;
};
} // namespace Detail

ConsoleRegistration::ConsoleRegistration(std::shared_ptr<Detail::ConsoleState> state, std::string name,
                                         std::uint64_t id)
    : m_state(std::move(state)), m_name(std::move(name)), m_id(id) {
}
ConsoleRegistration::~ConsoleRegistration() {
    Reset();
}
ConsoleRegistration::ConsoleRegistration(ConsoleRegistration&& other) noexcept
    : m_state(std::move(other.m_state)), m_name(std::move(other.m_name)), m_id(std::exchange(other.m_id, 0)) {
}
ConsoleRegistration& ConsoleRegistration::operator=(ConsoleRegistration&& other) noexcept {
    if (this != &other) {
        Reset();
        m_state = std::move(other.m_state);
        m_name = std::move(other.m_name);
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}
bool ConsoleRegistration::IsRegistered() const {
    auto state = m_state.lock();
    if (!state) {
        return false;
    }
    auto entry = state->entries.find(m_name);
    return entry != state->entries.end() && entry->second.id == m_id;
}
void ConsoleRegistration::Reset() {
    if (auto state = m_state.lock(); state && IsRegistered()) {
        state->entries.erase(m_name);
    }
    m_id = 0;
    m_state.reset();
}

Console::Console() : m_state(std::make_shared<Detail::ConsoleState>()) {
}
Console::~Console() = default;
Console& Console::Get() {
    static Console console;
    return console;
}

ConsoleRegistration Console::Register(ConsoleCommand command) {
    if (command.name.empty() || !command.execute || m_state->entries.contains(command.name) ||
        !std::ranges::all_of(command.name, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                   c == '-';
        })) {
        return {};
    }
    const auto id = m_state->nextId++;
    const auto name = command.name;
    m_state->entries.emplace(name, Detail::ConsoleState::Entry{std::move(command), id, {}, {}, {}});
    return {m_state, name, id};
}

ConsoleRegistration Console::RegisterVariable(std::string name, std::string description, ConsoleFlags flags,
                                              std::string defaultValue, std::function<std::string()> get,
                                              std::function<bool(std::string_view)> set) {
    if (!get || !set) {
        return {};
    }
    auto registration =
        Register({name, description, flags,
                  [name, description, defaultValue, get, set](std::span<const std::string> args) -> ConsoleResult {
                      if (args.empty()) {
                          return {true, name + " = " + get() + " (default " + defaultValue + ") " + description};
                      }
                      if (args.size() != 1 || !set(args.front())) {
                          return {false, "Invalid value for " + name};
                      }
                      return {true, name + " = " + get()};
                  }});
    if (registration.IsRegistered()) {
        auto& entry = m_state->entries.at(name);
        entry.defaultValue = std::move(defaultValue);
        entry.get = std::move(get);
        entry.set = std::move(set);
    }
    return registration;
}

ConsoleResult Console::Parse(std::string_view line, std::vector<std::string>& args) {
    args.clear();
    std::string word;
    char quote = 0;
    bool escaped = false, started = false;
    for (char c : line) {
        if (escaped) {
            word += c;
            escaped = false;
            started = true;
        } else if (c == '\\') {
            escaped = true;
            started = true;
        } else if (quote) {
            if (c == quote) {
                quote = 0;
            } else {
                word += c;
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
            started = true;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (started) {
                args.push_back(std::move(word));
                word.clear();
                started = false;
            }
        } else {
            word += c;
            started = true;
        }
    }
    if (quote || escaped) {
        args.clear();
        return {false, "Unterminated quote or escape"};
    }
    if (started) {
        args.push_back(std::move(word));
    }
    return {};
}

ConsoleResult Console::Execute(std::string_view line) {
    std::vector<std::string> args;
    auto result = Parse(line, args);
    if (result && args.empty()) {
        return {};
    }
    if (m_history.empty() || m_history.back() != line) {
        m_history.emplace_back(line);
    }
    if (m_history.size() > MaxHistory) {
        m_history.erase(m_history.begin());
    }
    if (result) {
        auto found = m_state->entries.find(args.front());
        if (found == m_state->entries.end()) {
            result = {false, "Unknown command: " + args.front()};
        } else if (HasFlag(found->second.command.flags, ConsoleFlags::Cheat) && !m_cheatsEnabled) {
            result = {false, "Cheats are disabled"};
        } else {
            // A command may unregister itself, change the registry or execute another command.
            auto execute = found->second.command.execute;
            try {
                result = execute(std::span(args).subspan(1));
            } catch (const std::exception& exception) {
                result = {false, exception.what()};
            } catch (...) {
                result = {false, "Command threw an unknown exception"};
            }
        }
    }
    m_output.push_back(result);
    if (m_output.size() > MaxOutput) {
        m_output.erase(m_output.begin());
    }
    return result;
}

std::vector<std::string> Console::Complete(std::string_view prefix) const {
    std::vector<std::string> result;
    for (const auto& [name, entry] : m_state->entries) {
        if (name.starts_with(prefix)) {
            result.push_back(name);
        }
    }
    return result;
}

void Console::Serialize(Archive& archive) {
    // Snapshot callbacks because a bound setter can unregister entries.
    std::vector<std::pair<std::string, Detail::ConsoleState::Entry>> saved;
    for (const auto& [name, entry] : m_state->entries) {
        if (entry.get && HasFlag(entry.command.flags, ConsoleFlags::Saved)) {
            saved.emplace_back(name, entry);
        }
    }
    for (const auto& [name, entry] : saved) {
        auto value = entry.get();
        archive.Field(name, value);
        if (archive.IsLoading() && !entry.set(value)) {
            throw std::runtime_error("Invalid saved value for " + name);
        }
    }
}

} // namespace Spot
