#include "temp_directory.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<std::filesystem::path> ListFiles(const std::filesystem::path& directory, std::string_view extension) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.path().extension() == extension) {
            files.push_back(entry.path());
        }
    }
    return files;
}

struct Crash {
    const char* mode;
    // What the log says happened.
    const char* what;
    // A function of the crash app on the stack, where symbols name them.
    const char* function;
};

} // namespace

TEST_CASE("Crashes are logged with their stack and leave a report", "[crash]") {
    const Crash crash = GENERATE(
        Crash{"access", "access violation writing", "CrashByAccess"},
        Crash{"pure", "pure virtual function", "CrashByPureCall"},
        Crash{"exception", "nobody catches this", "CrashByException"}, Crash{"abort", "the program aborted", "main"},
        Crash{"overflow", "stack overflow", "CrashByOverflow"}, Crash{"assert", "the test asked for it", "main"});
    CAPTURE(crash.mode);
    const TempDirectory temp;
    const auto log = temp.GetPath() / "crash.log";
    const auto reports = temp.GetPath() / "crashes";

    const std::vector<std::string> arguments{SPOT_CRASH_APP, crash.mode, Spot::PathToUtf8(log),
                                             Spot::PathToUtf8(reports)};
    auto process = Spot::Process::Start(arguments);
    REQUIRE(process);
    CHECK(process->Wait() != 0);

    // The log has what came before the crash, and the crash.
    const std::string text = ReadText(log);
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("About to crash"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Crash App crashed: "));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("Stack:"));

    const auto texts = ListFiles(reports, ".txt");
    REQUIRE(texts.size() == 1);
    const std::string report = ReadText(texts.front());
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring(Spot::PathToUtf8(texts.front())));
    CHECK_THAT(report, Catch::Matchers::ContainsSubstring("Crash App crashed: "));
#ifdef _MSC_VER
    // MSVC's symbols name every function; DbgHelp cannot read GCC's, and dladdr() only knows exported ones.
    CHECK_THAT(report, Catch::Matchers::ContainsSubstring(crash.what));
    CHECK_THAT(report, Catch::Matchers::ContainsSubstring(crash.function));
#endif
#ifdef _WIN32
    // Crashes that come as exceptions also leave a dump for debuggers.
    if (std::string_view(crash.mode) == "access" || std::string_view(crash.mode) == "overflow") {
        CHECK(ListFiles(reports, ".dmp").size() == 1);
    }
#endif
}

TEST_CASE("Applications keep the log of the run before", "[crash][application]") {
    const TempDirectory temp;
    const auto log = temp.GetPath() / "game.log";
    std::ofstream(log, std::ios::binary) << "the last run\n";
    {
        const Spot::Application application({.window = {.title = "Previous Log"}, .logFile = log});
        Spot::Log::Info("this run");
    }
    CHECK(ReadText(temp.GetPath() / "game.previous.log") == "the last run\n");
    CHECK_THAT(ReadText(log), Catch::Matchers::ContainsSubstring("this run"));
}
