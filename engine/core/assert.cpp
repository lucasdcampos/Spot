#include "core/assert.h"

#include "core/log.h"

#include <cstdlib>

namespace Spot::Detail {

void ReportAssertionFailure(std::string_view condition, std::string_view message,
                            const std::source_location& location) {
    Log::GetEngineLogger().Error("Assertion '{}' failed: {} ({}:{})", condition, message, location.file_name(),
                                 location.line());
    Log::Flush();
    std::abort();
}

} // namespace Spot::Detail
