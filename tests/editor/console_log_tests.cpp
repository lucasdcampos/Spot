#include "core/console_log.h"
#include "panels/console_panel.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

void Write(Spot::Editor::ConsoleLog& log, Spot::LogLevel level, std::string_view source, std::string_view message) {
    log.Write({.time = std::chrono::system_clock::now(), .level = level, .loggerName = source, .message = message});
}

std::vector<Spot::Editor::ConsoleEntry> FetchAll(const Spot::Editor::ConsoleLog& log) {
    std::vector<Spot::Editor::ConsoleEntry> entries;
    log.Fetch(0, entries);
    return entries;
}

} // namespace

TEST_CASE("The console log keeps messages in order with their level and source", "[editor][console]") {
    Spot::Editor::ConsoleLog log;
    Write(log, Spot::LogLevel::Info, "Spot", "first");
    Write(log, Spot::LogLevel::Error, "App", "second");

    const auto entries = FetchAll(log);

    REQUIRE(entries.size() == 2);
    CHECK(entries[0].message == "first");
    CHECK(entries[0].source == "Spot");
    CHECK(entries[1].level == Spot::LogLevel::Error);
    CHECK(entries[1].sequence > entries[0].sequence);
    const auto counts = log.GetCounts();
    CHECK(counts[static_cast<std::size_t>(Spot::LogLevel::Info)] == 1);
    CHECK(counts[static_cast<std::size_t>(Spot::LogLevel::Error)] == 1);
    REQUIRE(log.GetLatest().has_value());
    CHECK(log.GetLatest()->message == "second");
    Write(log, Spot::LogLevel::Trace, "Build", "detail");
    CHECK(log.GetLatest()->message == "detail");
    REQUIRE(log.GetLatest(Spot::LogLevel::Info).has_value());
    CHECK(log.GetLatest(Spot::LogLevel::Info)->message == "second");
}

TEST_CASE("The console log gives only the messages after the last one seen", "[editor][console]") {
    Spot::Editor::ConsoleLog log;
    Write(log, Spot::LogLevel::Info, "Spot", "old");
    std::vector<Spot::Editor::ConsoleEntry> entries;
    log.Fetch(0, entries);
    Write(log, Spot::LogLevel::Info, "Spot", "new");

    CHECK(log.Fetch(entries.back().sequence, entries) == 1);

    REQUIRE(entries.size() == 2);
    CHECK(entries[1].message == "new");
}

TEST_CASE("The console log drops its oldest messages beyond its capacity", "[editor][console]") {
    Spot::Editor::ConsoleLog log(3);
    for (int i = 0; i < 5; ++i) {
        Write(log, i == 0 ? Spot::LogLevel::Warning : Spot::LogLevel::Info, "Spot", std::to_string(i));
    }

    const auto entries = FetchAll(log);

    REQUIRE(entries.size() == 3);
    CHECK(entries.front().message == "2");
    CHECK(log.GetCounts()[static_cast<std::size_t>(Spot::LogLevel::Warning)] == 0);
    CHECK(log.GetCounts()[static_cast<std::size_t>(Spot::LogLevel::Info)] == 3);
}

TEST_CASE("Clearing the console log empties it and tells readers", "[editor][console]") {
    Spot::Editor::ConsoleLog log;
    Write(log, Spot::LogLevel::Info, "Spot", "message");
    const auto clears = log.GetClearCount();

    log.Clear();

    CHECK(log.GetSize() == 0);
    CHECK_FALSE(log.GetLatest().has_value());
    CHECK(log.GetClearCount() != clears);
}

TEST_CASE("The console log takes messages from several threads at once", "[editor][console]") {
    Spot::Editor::ConsoleLog log(100000);
    constexpr int Threads = 4;
    constexpr int PerThread = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < Threads; ++t) {
        threads.emplace_back([&log] {
            for (int i = 0; i < PerThread; ++i) {
                Write(log, Spot::LogLevel::Info, "Worker", "message");
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    const auto entries = FetchAll(log);
    REQUIRE(entries.size() == Threads * PerThread);
    for (std::size_t i = 1; i < entries.size(); ++i) {
        CHECK(entries[i].sequence == entries[i - 1].sequence + 1);
    }
}

TEST_CASE("Console rows follow the level, source and text filters", "[editor][console]") {
    Spot::Editor::ConsoleLog log;
    Write(log, Spot::LogLevel::Debug, "Spot", "Device created");
    Write(log, Spot::LogLevel::Warning, "App", "Low health");
    Write(log, Spot::LogLevel::Error, "Spot", "Texture missing");
    const auto entries = FetchAll(log);

    Spot::Editor::ConsoleFilter filter;
    CHECK(Spot::Editor::BuildConsoleRows(entries, filter).size() == 3);

    filter.levels[static_cast<std::size_t>(Spot::LogLevel::Debug)] = false;
    CHECK(Spot::Editor::BuildConsoleRows(entries, filter).size() == 2);

    filter.source = "Spot";
    auto rows = Spot::Editor::BuildConsoleRows(entries, filter);
    REQUIRE(rows.size() == 1);
    CHECK(entries[rows[0].entry].message == "Texture missing");

    filter = {};
    filter.text = "HEALTH";
    rows = Spot::Editor::BuildConsoleRows(entries, filter);
    REQUIRE(rows.size() == 1);
    CHECK(entries[rows[0].entry].source == "App");
}

TEST_CASE("Collapsed console rows count identical messages once", "[editor][console]") {
    Spot::Editor::ConsoleLog log;
    Write(log, Spot::LogLevel::Info, "App", "tick");
    Write(log, Spot::LogLevel::Info, "App", "other");
    Write(log, Spot::LogLevel::Info, "App", "tick");
    Write(log, Spot::LogLevel::Warning, "App", "tick");
    const auto entries = FetchAll(log);

    Spot::Editor::ConsoleFilter filter;
    filter.collapse = true;
    const auto rows = Spot::Editor::BuildConsoleRows(entries, filter);

    // The warning differs by level, so it stays apart.
    REQUIRE(rows.size() == 3);
    CHECK(entries[rows[0].entry].message == "tick");
    CHECK(rows[0].count == 2);
    CHECK(rows[1].count == 1);
    CHECK(entries[rows[2].entry].level == Spot::LogLevel::Warning);
}

TEST_CASE("Console entries format like log file lines", "[editor][console]") {
    Spot::Editor::ConsoleLog log;
    Write(log, Spot::LogLevel::Warning, "Spot", "Careful");

    const std::string line = Spot::Editor::FormatConsoleEntry(FetchAll(log).front());

    CHECK_THAT(line, Catch::Matchers::EndsWith("[WARN ] [Spot] Careful"));
}

TEST_CASE("The console panel follows its log, including clears", "[editor][console]") {
    auto log = std::make_shared<Spot::Editor::ConsoleLog>();
    Spot::Editor::ConsolePanel panel(log);
    Write(*log, Spot::LogLevel::Info, "Spot", "one");
    Write(*log, Spot::LogLevel::Trace, "Spot", "hidden by default");

    panel.Sync();
    CHECK(panel.GetRows().size() == 1);

    Write(*log, Spot::LogLevel::Info, "Spot", "two");
    panel.Sync();
    CHECK(panel.GetRows().size() == 2);

    log->Clear();
    panel.Sync();
    CHECK(panel.GetRows().empty());
}
