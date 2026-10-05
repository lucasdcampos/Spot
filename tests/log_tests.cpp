#include <spot.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

struct CapturedRecord {
    Spot::LogLevel level;
    std::string loggerName;
    std::string message;
};

class CaptureSink final : public Spot::LogSink {
public:
    void Write(const Spot::LogRecord& record) override {
        m_records.push_back({record.level, std::string(record.loggerName), std::string(record.message)});
    }

    const std::vector<CapturedRecord>& GetRecords() const { return m_records; }

private:
    std::vector<CapturedRecord> m_records;
};

// Replaces the global sinks for the duration of a test so tests neither print nor see each other's output.
class ScopedSinks {
public:
    explicit ScopedSinks(std::vector<std::shared_ptr<Spot::LogSink>> sinks) : m_previous(Spot::Log::GetSinks()) {
        Spot::Log::SetSinks(std::move(sinks));
    }

    ~ScopedSinks() { Spot::Log::SetSinks(std::move(m_previous)); }

    ScopedSinks(const ScopedSinks&) = delete;
    ScopedSinks& operator=(const ScopedSinks&) = delete;

private:
    std::vector<std::shared_ptr<Spot::LogSink>> m_previous;
};

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Logger formats the message and tags it with its name and level", "[log]") {
    const auto capture = std::make_shared<CaptureSink>();
    const ScopedSinks sinks({capture});

    Spot::Logger logger("Test", Spot::LogLevel::Trace);
    logger.Info("{} + {} = {}", 1, 2, 3);

    REQUIRE(capture->GetRecords().size() == 1);
    const auto& record = capture->GetRecords().front();
    CHECK(record.level == Spot::LogLevel::Info);
    CHECK(record.loggerName == "Test");
    CHECK(record.message == "1 + 2 = 3");
}

TEST_CASE("Logger skips messages below its level", "[log]") {
    const auto capture = std::make_shared<CaptureSink>();
    const ScopedSinks sinks({capture});

    Spot::Logger logger("Test", Spot::LogLevel::Warning);
    logger.Trace("trace");
    logger.Debug("debug");
    logger.Info("info");
    logger.Warning("warning");
    logger.Error("error");

    REQUIRE(capture->GetRecords().size() == 2);
    CHECK(capture->GetRecords()[0].level == Spot::LogLevel::Warning);
    CHECK(capture->GetRecords()[1].level == Spot::LogLevel::Error);

    logger.SetLevel(Spot::LogLevel::Trace);
    logger.Trace("trace");
    CHECK(capture->GetRecords().size() == 3);
}

TEST_CASE("Log functions write through the app logger", "[log]") {
    const auto capture = std::make_shared<CaptureSink>();
    const ScopedSinks sinks({capture});

    Spot::Log::Info("hello {}", "world");

    REQUIRE(capture->GetRecords().size() == 1);
    CHECK(capture->GetRecords().front().loggerName == "App");
    CHECK(capture->GetRecords().front().message == "hello world");
    CHECK(Spot::Log::GetEngineLogger().GetName() == "Spot");
}

TEST_CASE("Every registered sink receives each record until it is removed", "[log]") {
    const auto first = std::make_shared<CaptureSink>();
    const auto second = std::make_shared<CaptureSink>();
    const ScopedSinks sinks({first});
    Spot::Logger logger("Test", Spot::LogLevel::Trace);

    Spot::Log::AddSink(second);
    logger.Info("both");
    Spot::Log::RemoveSink(second);
    logger.Info("first only");

    CHECK(first->GetRecords().size() == 2);
    CHECK(second->GetRecords().size() == 1);
}

TEST_CASE("FormatLogRecord prefixes the message with time, level and logger name", "[log]") {
    std::tm local{};
    local.tm_year = 2026 - 1900;
    local.tm_mon = 9;
    local.tm_mday = 4;
    local.tm_hour = 14;
    local.tm_min = 3;
    local.tm_sec = 7;
    local.tm_isdst = -1;
    const auto time = std::chrono::system_clock::from_time_t(std::mktime(&local)) + std::chrono::milliseconds(45);

    const Spot::LogRecord record{time, Spot::LogLevel::Warning, "Spot", "Disk almost full"};

    CHECK(Spot::FormatLogRecord(record) == "[14:03:07.045] [WARN ] [Spot] Disk almost full");
}

TEST_CASE("FileSink writes formatted records to a new file", "[log]") {
    const auto directory = std::filesystem::temp_directory_path() / "spot_tests" / "file_sink";
    const auto path = directory / "nested" / "test.log";
    std::filesystem::remove_all(directory);

    {
        const auto file = std::make_shared<Spot::FileSink>(path);
        REQUIRE(file->IsOpen());
        const ScopedSinks sinks({file});

        Spot::Logger logger("File", Spot::LogLevel::Trace);
        logger.Info("first");
        logger.Error("second");
    }

    const auto contents = ReadFile(path);
    CHECK_THAT(contents, Catch::Matchers::Matches(R"(\[[0-9:.]{12}\] \[INFO \] \[File\] first\n)"
                                                  R"(\[[0-9:.]{12}\] \[ERROR\] \[File\] second\n)"));

    {
        const Spot::FileSink truncated(path);
        REQUIRE(truncated.IsOpen());
    }
    CHECK(ReadFile(path).empty());

    std::filesystem::remove_all(directory);
}

TEST_CASE("SPOT_ASSERT does nothing when the condition holds", "[log]") {
    const auto capture = std::make_shared<CaptureSink>();
    const ScopedSinks sinks({capture});

    const int value = 2;
    SPOT_ASSERT(value == 2, "value should be {}", 2);

    CHECK(capture->GetRecords().empty());
}
