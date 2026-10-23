#include "spot/platform/crash_handler.h"

#include "spot/core/assert.h"
#include "spot/core/log.h"
#include "spot/core/paths.h"
#include "spot/platform/crash_handler_internal.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <format>
#include <fstream>
#include <system_error>

namespace Spot {

namespace {

CrashHandlerConfig& GetConfig() {
    static CrashHandlerConfig config;
    return config;
}

std::atomic<bool> s_crashed = false;
bool s_installed = false;

std::tm GetLocalTime() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local;
}

[[noreturn]] void OnTerminate() {
    std::string what = "std::terminate() was called";
    if (const std::exception_ptr exception = std::current_exception()) {
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& error) {
            what = std::format("An exception was not caught: {}", error.what());
        } catch (...) {
            what = "An exception of an unknown type was not caught";
        }
    }
    Detail::ReportFatalError(what);
    std::abort();
}

// Failed assertions write crash reports whenever the crash handler is linked, installed or not, as they did when it
// was part of the core.
[[maybe_unused]] const bool s_fatalErrorHandlerSet = (Detail::SetFatalErrorHandler(Detail::ReportFatalError), true);

} // namespace

void InstallCrashHandler(const CrashHandlerConfig& config) {
    GetConfig() = config;
    if (s_installed) {
        return;
    }
    s_installed = true;
    std::set_terminate(OnTerminate);
    Detail::InstallPlatformCrashHandlers();
}

namespace Detail {

const CrashHandlerConfig& GetCrashHandlerConfig() {
    return GetConfig();
}

bool BeginCrash() {
    return !s_crashed.exchange(true);
}

void ReportFatalError(std::string_view what) {
    if (BeginCrash()) {
        const std::filesystem::path report = ReportCrash(what, CaptureStack());
        ShowCrashMessage(what, report);
    }
}

std::filesystem::path GetCrashReportPath(std::string_view extension) {
    const CrashHandlerConfig& config = GetConfig();
    if (config.reportDirectory.empty()) {
        return {};
    }
    const std::tm time = GetLocalTime();
    const std::string name = config.applicationName.empty() ? "crash" : config.applicationName;
    return config.reportDirectory /
           Utf8ToPath(std::format("{}-{:04}{:02}{:02}-{:02}{:02}{:02}{}", name, time.tm_year + 1900, time.tm_mon + 1,
                                  time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, extension));
}

std::filesystem::path ReportCrash(std::string_view what, std::string_view stack) {
    const CrashHandlerConfig& config = GetConfig();
    Log::Detail::WriteDuringCrash(LogLevel::Error, "Crash",
                                  std::format("{} crashed: {}\nStack:\n{}",
                                              config.applicationName.empty() ? "The program" : config.applicationName,
                                              what, stack));

    std::filesystem::path report = GetCrashReportPath(".txt");
    if (report.empty()) {
        return {};
    }
    std::error_code error;
    std::filesystem::create_directories(report.parent_path(), error);
    std::ofstream file(report, std::ios::binary);
    file << std::format("{} crashed: {}\n\nStack:\n{}", config.applicationName, what, stack);
    file.close();
    if (!file) {
        Log::Detail::WriteDuringCrash(LogLevel::Error, "Crash",
                                      std::format("Could not write the crash report '{}'", PathToUtf8(report)));
        return {};
    }
    Log::Detail::WriteDuringCrash(LogLevel::Error, "Crash", std::format("Crash report: '{}'", PathToUtf8(report)));
    return report;
}

} // namespace Detail

} // namespace Spot
