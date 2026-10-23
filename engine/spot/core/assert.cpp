#include "spot/core/assert.h"

#include "spot/core/log.h"

#include <atomic>
#include <cstdlib>
#include <format>

namespace Spot::Detail {

namespace {

void LogFatalError(std::string_view what) {
    Log::Detail::WriteDuringCrash(LogLevel::Error, "Crash", what);
}

std::atomic<FatalErrorHandler> s_fatalErrorHandler = LogFatalError;

} // namespace

void SetFatalErrorHandler(FatalErrorHandler handler) {
    s_fatalErrorHandler = handler != nullptr ? handler : LogFatalError;
}

void ReportAssertionFailure(std::string_view condition, std::string_view message,
                            const std::source_location& location) {
    s_fatalErrorHandler.load()(
        std::format("assertion '{}' failed: {} ({}:{})", condition, message, location.file_name(), location.line()));
    std::abort();
}

} // namespace Spot::Detail
