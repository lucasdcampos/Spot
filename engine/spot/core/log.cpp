#include "spot/core/log.h"

#include "spot/core/log_sinks.h"

#include <ctime>
#include <mutex>
#include <thread>

namespace Spot {

namespace {

#ifdef NDEBUG
constexpr LogLevel DefaultLevel = LogLevel::Info;
#else
constexpr LogLevel DefaultLevel = LogLevel::Debug;
#endif

struct SinkRegistry {
    std::mutex mutex;
    std::vector<std::shared_ptr<LogSink>> sinks{std::make_shared<ConsoleSink>()};
};

SinkRegistry& GetSinkRegistry() {
    static SinkRegistry registry;
    return registry;
}

std::tm ToLocalTime(std::time_t time) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

} // namespace

std::string_view GetLogLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return {};
}

std::string FormatLogRecord(const LogRecord& record) {
    const auto local = ToLocalTime(std::chrono::system_clock::to_time_t(record.time));
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(record.time.time_since_epoch());
    return std::format("[{:02}:{:02}:{:02}.{:03}] [{:<5}] [{}] {}", local.tm_hour, local.tm_min, local.tm_sec,
                       sinceEpoch.count() % 1000, GetLogLevelName(record.level), record.loggerName, record.message);
}

Logger::Logger(std::string name) : Logger(std::move(name), DefaultLevel) {
}

Logger::Logger(std::string name, LogLevel level) : m_name(std::move(name)), m_level(level) {
}

void Logger::WriteMessage(LogLevel level, std::string_view message) {
    if (!IsEnabled(level)) {
        return;
    }

    const LogRecord record{std::chrono::system_clock::now(), level, m_name, message};
    auto& registry = GetSinkRegistry();
    const std::scoped_lock lock(registry.mutex);
    for (const auto& sink : registry.sinks) {
        sink->Write(record);
    }
}

namespace Log {

void AddSink(std::shared_ptr<LogSink> sink) {
    if (!sink) {
        return;
    }

    auto& registry = GetSinkRegistry();
    const std::scoped_lock lock(registry.mutex);
    registry.sinks.push_back(std::move(sink));
}

void RemoveSink(const std::shared_ptr<LogSink>& sink) {
    auto& registry = GetSinkRegistry();
    const std::scoped_lock lock(registry.mutex);
    std::erase(registry.sinks, sink);
}

std::vector<std::shared_ptr<LogSink>> GetSinks() {
    auto& registry = GetSinkRegistry();
    const std::scoped_lock lock(registry.mutex);
    return registry.sinks;
}

void SetSinks(std::vector<std::shared_ptr<LogSink>> sinks) {
    std::erase(sinks, nullptr);

    auto& registry = GetSinkRegistry();
    const std::scoped_lock lock(registry.mutex);
    registry.sinks.swap(sinks);
}

void Flush() {
    auto& registry = GetSinkRegistry();
    const std::scoped_lock lock(registry.mutex);
    for (const auto& sink : registry.sinks) {
        sink->Flush();
    }
}

void Detail::WriteDuringCrash(LogLevel level, std::string_view loggerName, std::string_view message) {
    auto& registry = GetSinkRegistry();
    bool locked = false;
    for (int attempt = 0; attempt < 100 && !locked; ++attempt) {
        locked = registry.mutex.try_lock();
        if (!locked) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    const LogRecord record{std::chrono::system_clock::now(), level, loggerName, message};
    for (const auto& sink : registry.sinks) {
        sink->Write(record);
        sink->Flush();
    }
    if (locked) {
        registry.mutex.unlock();
    }
}

Logger& GetEngineLogger() {
    static Logger logger("Spot");
    return logger;
}

Logger& GetAppLogger() {
    static Logger logger("App");
    return logger;
}

} // namespace Log

} // namespace Spot
