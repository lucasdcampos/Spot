#pragma once

#include "spot/platform/crash_handler.h"

#include <filesystem>
#include <string>
#include <string_view>

// What the crash handler's platform code shares with the rest of it.
namespace Spot::Detail {

const CrashHandlerConfig& GetCrashHandlerConfig();
// True for the first crash only: a crash while handling one, or in another thread meanwhile, is not reported again.
bool BeginCrash();
// Logs the crash with its stack and writes the text report, returning its path, or an empty one when none was
// written.
std::filesystem::path ReportCrash(std::string_view what, std::string_view stack);
// Where a new report of the crash goes, with the extension: <directory>/<name>-<YYYYMMDD-HHMMSS><extension>. Empty
// without a report directory.
std::filesystem::path GetCrashReportPath(std::string_view extension);

// Reports a crash the engine itself finds, such as a failed assertion, from where it happened, unless one was
// reported already. The caller ends the process.
void ReportFatalError(std::string_view what);

// Implemented by each platform.
void InstallPlatformCrashHandlers();
// The calling thread's stack, a frame per line, from the caller of the function.
std::string CaptureStack();
void ShowCrashMessage(std::string_view what, const std::filesystem::path& report);

} // namespace Spot::Detail
