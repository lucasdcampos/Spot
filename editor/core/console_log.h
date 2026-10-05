#pragma once

#include <spot.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Editor {

struct ConsoleEntry {
    // Increases by one with every message, so readers can ask for what came after the last one they saw.
    std::uint64_t sequence = 0;
    std::chrono::system_clock::time_point time;
    LogLevel level = LogLevel::Info;
    // The name of the logger, such as "Spot", "App" or "Editor".
    std::string source;
    std::string message;
};

// Keeps the latest log messages for the console, as a sink added to the engine's log. Messages may come from any
// thread, so every member is safe to call concurrently.
class ConsoleLog final : public LogSink {
public:
    static constexpr std::size_t DefaultCapacity = 10000;

    // The oldest messages are dropped beyond the capacity.
    explicit ConsoleLog(std::size_t capacity = DefaultCapacity);

    void Write(const LogRecord& record) override;

    // Appends the messages that came after the given sequence, oldest first, and returns how many there were.
    std::size_t Fetch(std::uint64_t after, std::vector<ConsoleEntry>& entries) const;
    std::optional<ConsoleEntry> GetLatest() const;
    // Messages kept per level, indexed by LogLevel.
    std::array<std::size_t, 5> GetCounts() const;
    std::size_t GetSize() const;
    std::size_t GetCapacity() const { return m_capacity; }

    void Clear();
    // Changes whenever the console is cleared, so readers know to drop what they fetched.
    std::uint64_t GetClearCount() const;

private:
    mutable std::mutex m_mutex;
    std::size_t m_capacity;
    std::deque<ConsoleEntry> m_entries;
    std::array<std::size_t, 5> m_counts{};
    std::uint64_t m_nextSequence = 1;
    std::uint64_t m_clearCount = 0;
};

struct ConsoleFilter {
    // Indexed by LogLevel.
    std::array<bool, 5> levels = {true, true, true, true, true};
    // Matched anywhere in the message or the source, ignoring case; empty matches everything.
    std::string text;
    // Empty matches every source.
    std::string source;
    // Shows identical messages once, with how many times they came.
    bool collapse = false;
};

struct ConsoleRow {
    // Index of the first matching entry.
    std::size_t entry = 0;
    std::size_t count = 1;
};

bool MatchesFilter(const ConsoleEntry& entry, const ConsoleFilter& filter);
// The rows the console shows for the entries, in the order the entries came.
std::vector<ConsoleRow> BuildConsoleRows(const std::vector<ConsoleEntry>& entries, const ConsoleFilter& filter);
// "[14:03:12.345] [WARN ] [Spot] message", as the log file has it.
std::string FormatConsoleEntry(const ConsoleEntry& entry);

} // namespace Spot::Editor
