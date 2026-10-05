#pragma once

#include <spot.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Spot::Tests {

struct CapturedRecord {
    LogLevel level;
    std::string loggerName;
    std::string message;
};

class CaptureSink final : public LogSink {
public:
    void Write(const LogRecord& record) override {
        m_records.push_back({record.level, std::string(record.loggerName), std::string(record.message)});
    }

    const std::vector<CapturedRecord>& GetRecords() const { return m_records; }

private:
    std::vector<CapturedRecord> m_records;
};

// Replaces the global sinks for the duration of a test so tests neither print nor see each other's output.
class ScopedSinks {
public:
    explicit ScopedSinks(std::vector<std::shared_ptr<LogSink>> sinks) : m_previous(Log::GetSinks()) {
        Log::SetSinks(std::move(sinks));
    }

    ~ScopedSinks() { Log::SetSinks(std::move(m_previous)); }

    ScopedSinks(const ScopedSinks&) = delete;
    ScopedSinks& operator=(const ScopedSinks&) = delete;

private:
    std::vector<std::shared_ptr<LogSink>> m_previous;
};

} // namespace Spot::Tests
