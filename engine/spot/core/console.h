#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Spot {

class Archive;
enum class ConsoleFlags : std::uint8_t { None = 0, Cheat = 1, Saved = 2 };
constexpr ConsoleFlags operator|(ConsoleFlags first, ConsoleFlags second) {
    return static_cast<ConsoleFlags>(static_cast<unsigned>(first) | static_cast<unsigned>(second));
}
constexpr bool HasFlag(ConsoleFlags flags, ConsoleFlags flag) {
    return (static_cast<unsigned>(flags) & static_cast<unsigned>(flag)) != 0;
}

struct ConsoleResult {
    bool success = true;
    std::string message;
    explicit operator bool() const { return success; }
};

struct ConsoleCommand {
    std::string name;
    std::string description;
    ConsoleFlags flags = ConsoleFlags::None;
    std::function<ConsoleResult(std::span<const std::string>)> execute;
};

namespace Detail {
struct ConsoleState;
}

// Registrations outlive neither their owner nor their registry. Moving one transfers that ownership.
class ConsoleRegistration {
public:
    ConsoleRegistration() = default;
    ~ConsoleRegistration();
    ConsoleRegistration(ConsoleRegistration&& other) noexcept;
    ConsoleRegistration& operator=(ConsoleRegistration&& other) noexcept;
    ConsoleRegistration(const ConsoleRegistration&) = delete;
    ConsoleRegistration& operator=(const ConsoleRegistration&) = delete;
    void Reset();
    bool IsRegistered() const;

private:
    friend class Console;
    ConsoleRegistration(std::shared_ptr<Detail::ConsoleState> state, std::string name, std::uint64_t id);
    std::weak_ptr<Detail::ConsoleState> m_state;
    std::string m_name;
    std::uint64_t m_id = 0;
};

template <typename T>
bool ParseConsoleValue(std::string_view text, T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        value = text;
        return true;
    } else if constexpr (std::is_same_v<T, bool>) {
        if (text == "true" || text == "1") {
            value = true;
            return true;
        }
        if (text == "false" || text == "0") {
            value = false;
            return true;
        }
        return false;
    } else {
        static_assert(std::is_arithmetic_v<T>);
        T parsed{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
            return false;
        }
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(parsed)) {
                return false;
            }
        }
        value = parsed;
        return true;
    }
}

template <typename T>
std::string FormatConsoleValue(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else {
        char buffer[128];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
    }
}

// Independent registries support tools and headless tests. Get() is the process-wide runtime registry.
class Console {
public:
    Console();
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;
    static Console& Get();
    ConsoleRegistration Register(ConsoleCommand command);
    ConsoleRegistration RegisterVariable(std::string name, std::string description, ConsoleFlags flags,
                                         std::string defaultValue, std::function<std::string()> get,
                                         std::function<bool(std::string_view)> set);
    ConsoleResult Execute(std::string_view line);
    std::vector<std::string> Complete(std::string_view prefix) const;
    const std::vector<std::string>& GetHistory() const { return m_history; }
    const std::vector<ConsoleResult>& GetOutput() const { return m_output; }
    void ClearOutput() { m_output.clear(); }
    void SetCheatsEnabled(bool enabled) { m_cheatsEnabled = enabled; }
    bool AreCheatsEnabled() const { return m_cheatsEnabled; }
    // Saved cvars are string fields; their setters validate values before applying them.
    void Serialize(Archive& archive);
    static ConsoleResult Parse(std::string_view line, std::vector<std::string>& arguments);
    static constexpr std::size_t MaxHistory = 128;
    static constexpr std::size_t MaxOutput = 256;

private:
    std::shared_ptr<Detail::ConsoleState> m_state;
    std::vector<std::string> m_history;
    std::vector<ConsoleResult> m_output;
    bool m_cheatsEnabled = false;
};

// A cvar can own a value or bind to existing engine settings. Bindings must outlive the cvar.
template <typename T>
class CVar {
public:
    using Validator = std::function<bool(const T&)>;
    CVar(Console& console, std::string name, T defaultValue, std::string description = {},
         ConsoleFlags flags = ConsoleFlags::None, Validator validate = {})
        : m_value(std::make_shared<T>(defaultValue)) {
        auto value = m_value;
        Register(
            console, std::move(name), defaultValue, std::move(description), flags, [value] { return *value; },
            [value](const T& next) { *value = next; }, std::move(validate));
    }
    CVar(Console& console, std::string name, T defaultValue, std::string description, ConsoleFlags flags,
         std::function<T()> get, std::function<void(const T&)> set, Validator validate = {}) {
        Register(console, std::move(name), defaultValue, std::move(description), flags, std::move(get), std::move(set),
                 std::move(validate));
    }
    T Get() const { return m_get(); }
    bool Set(const T& value) { return m_set(FormatConsoleValue(value)); }
    bool IsRegistered() const { return m_registration.IsRegistered(); }

private:
    void Register(Console& console, std::string name, T defaultValue, std::string description, ConsoleFlags flags,
                  std::function<T()> get, std::function<void(const T&)> set, Validator validate) {
        m_get = std::move(get);
        m_set = [set = std::move(set), validate = std::move(validate)](std::string_view text) {
            T next{};
            if (!ParseConsoleValue(text, next) || (validate && !validate(next))) {
                return false;
            }
            set(next);
            return true;
        };
        m_registration = console.RegisterVariable(
            std::move(name), std::move(description), flags, FormatConsoleValue(defaultValue),
            [get = m_get] { return FormatConsoleValue(get()); }, m_set);
    }
    std::shared_ptr<T> m_value;
    std::function<T()> m_get;
    std::function<bool(std::string_view)> m_set;
    ConsoleRegistration m_registration;
};

} // namespace Spot
