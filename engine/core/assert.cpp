#include "core/assert.h"

#include "core/crash_handler_internal.h"

#include <cstdlib>
#include <format>

namespace Spot::Detail {

void ReportAssertionFailure(std::string_view condition, std::string_view message,
                            const std::source_location& location) {
    ReportFatalError(
        std::format("assertion '{}' failed: {} ({}:{})", condition, message, location.file_name(), location.line()));
    std::abort();
}

} // namespace Spot::Detail
