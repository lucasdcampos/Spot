#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Spot {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

struct LogRecord {
    std::chrono::system_clock::time_point time;
    LogLevel level;
    std::string_view loggerName;
    std::string_view message;
};

std::string_view GetLogLevelName(LogLevel level);

// Produces "[14:03:12.345] [INFO ] [Spot] message", with the time in the local time zone.
std::string FormatLogRecord(const LogRecord& record);

class LogSink {
public:
    virtual ~LogSink() = default;

    // Called with the sink registry locked, so sinks need no synchronization of their own.
    virtual void Write(const LogRecord& record) = 0;
    virtual void Flush() {}
};

class Logger {
public:
    explicit Logger(std::string name);
    Logger(std::string name, LogLevel level);

    const std::string& GetName() const { return m_name; }
    LogLevel GetLevel() const { return m_level.load(std::memory_order_relaxed); }
    void SetLevel(LogLevel level) { m_level.store(level, std::memory_order_relaxed); }
    bool IsEnabled(LogLevel level) const { return level >= GetLevel(); }

    template <typename... Args>
    void Trace(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Trace, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Debug(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Debug, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Info(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Info, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Warning(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Warning, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Error(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Error, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Write(LogLevel level, std::format_string<Args...> format, Args&&... args) {
        // Checked before formatting so disabled levels cost almost nothing.
        if (IsEnabled(level)) {
            WriteMessage(level, std::format(format, std::forward<Args>(args)...));
        }
    }

    void WriteMessage(LogLevel level, std::string_view message);

private:
    std::string m_name;
    std::atomic<LogLevel> m_level;
};

namespace Log {

// Sinks are shared by every logger, so outputs are configured once for the whole process.
// A console sink is registered by default.
void AddSink(std::shared_ptr<LogSink> sink);
void RemoveSink(const std::shared_ptr<LogSink>& sink);
std::vector<std::shared_ptr<LogSink>> GetSinks();
void SetSinks(std::vector<std::shared_ptr<LogSink>> sinks);
void Flush();

Logger& GetEngineLogger();
Logger& GetAppLogger();

template <typename... Args>
void Trace(std::format_string<Args...> format, Args&&... args) {
    GetAppLogger().Trace(format, std::forward<Args>(args)...);
}

template <typename... Args>
void Debug(std::format_string<Args...> format, Args&&... args) {
    GetAppLogger().Debug(format, std::forward<Args>(args)...);
}

template <typename... Args>
void Info(std::format_string<Args...> format, Args&&... args) {
    GetAppLogger().Info(format, std::forward<Args>(args)...);
}

template <typename... Args>
void Warning(std::format_string<Args...> format, Args&&... args) {
    GetAppLogger().Warning(format, std::forward<Args>(args)...);
}

template <typename... Args>
void Error(std::format_string<Args...> format, Args&&... args) {
    GetAppLogger().Error(format, std::forward<Args>(args)...);
}

} // namespace Log

} // namespace Spot
