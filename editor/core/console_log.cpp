#include "core/console_log.h"

#include "core/text.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <unordered_map>

namespace Spot::Editor {

namespace {

std::size_t GetLevelIndex(LogLevel level) {
    return static_cast<std::size_t>(level);
}

} // namespace

ConsoleLog::ConsoleLog(std::size_t capacity) : m_capacity(std::max<std::size_t>(capacity, 1)) {
}

void ConsoleLog::Write(const LogRecord& record) {
    const std::scoped_lock lock(m_mutex);
    if (m_entries.size() == m_capacity) {
        --m_counts[GetLevelIndex(m_entries.front().level)];
        m_entries.pop_front();
    }
    m_entries.push_back({.sequence = m_nextSequence++,
                         .time = record.time,
                         .level = record.level,
                         .source = std::string(record.loggerName),
                         .message = std::string(record.message)});
    ++m_counts[GetLevelIndex(record.level)];
}

std::size_t ConsoleLog::Fetch(std::uint64_t after, std::vector<ConsoleEntry>& entries) const {
    const std::scoped_lock lock(m_mutex);
    // Sequences increase along the deque, so the new messages are a suffix of it.
    const auto first = std::ranges::upper_bound(m_entries, after, {}, &ConsoleEntry::sequence);
    entries.insert(entries.end(), first, m_entries.end());
    return static_cast<std::size_t>(std::distance(first, m_entries.end()));
}

std::optional<ConsoleEntry> ConsoleLog::GetLatest(LogLevel minimum) const {
    const std::scoped_lock lock(m_mutex);
    const auto found = std::find_if(m_entries.rbegin(), m_entries.rend(),
                                    [&](const ConsoleEntry& entry) { return entry.level >= minimum; });
    if (found == m_entries.rend()) {
        return std::nullopt;
    }
    return *found;
}

std::array<std::size_t, 5> ConsoleLog::GetCounts() const {
    const std::scoped_lock lock(m_mutex);
    return m_counts;
}

std::size_t ConsoleLog::GetSize() const {
    const std::scoped_lock lock(m_mutex);
    return m_entries.size();
}

void ConsoleLog::Clear() {
    const std::scoped_lock lock(m_mutex);
    m_entries.clear();
    m_counts = {};
    ++m_clearCount;
}

std::uint64_t ConsoleLog::GetClearCount() const {
    const std::scoped_lock lock(m_mutex);
    return m_clearCount;
}

bool MatchesFilter(const ConsoleEntry& entry, const ConsoleFilter& filter) {
    if (!filter.levels[GetLevelIndex(entry.level)]) {
        return false;
    }
    if (!filter.source.empty() && entry.source != filter.source) {
        return false;
    }
    return ContainsIgnoringCase(entry.message, filter.text) || ContainsIgnoringCase(entry.source, filter.text);
}

std::vector<ConsoleRow> BuildConsoleRows(const std::vector<ConsoleEntry>& entries, const ConsoleFilter& filter) {
    std::vector<ConsoleRow> rows;
    // Collapsed rows are found by level, source and message; the message alone is hashed.
    std::unordered_multimap<std::string_view, std::size_t> collapsed;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ConsoleEntry& entry = entries[i];
        if (!MatchesFilter(entry, filter)) {
            continue;
        }
        if (filter.collapse) {
            const auto [begin, end] = collapsed.equal_range(entry.message);
            const auto same = std::find_if(begin, end, [&](const auto& candidate) {
                const ConsoleEntry& first = entries[rows[candidate.second].entry];
                return first.level == entry.level && first.source == entry.source;
            });
            if (same != end) {
                ++rows[same->second].count;
                continue;
            }
            collapsed.emplace(entry.message, rows.size());
        }
        rows.push_back({.entry = i, .count = 1});
    }
    return rows;
}

std::string FormatConsoleEntry(const ConsoleEntry& entry) {
    return FormatLogRecord(
        {.time = entry.time, .level = entry.level, .loggerName = entry.source, .message = entry.message});
}

} // namespace Spot::Editor
