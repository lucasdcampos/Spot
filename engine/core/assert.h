#pragma once

#include <format>
#include <source_location>
#include <string_view>
#include <utility>

namespace Spot::Detail {

[[noreturn]] void ReportAssertionFailure(std::string_view condition, std::string_view message,
                                         const std::source_location& location);

template <typename... Args>
[[noreturn]] void AssertionFailed(std::string_view condition, const std::source_location& location,
                                  std::format_string<Args...> format, Args&&... args) {
    ReportAssertionFailure(condition, std::format(format, std::forward<Args>(args)...), location);
}

} // namespace Spot::Detail

// Logs the failure and aborts. Compiled out in release builds; the condition is not evaluated there.
#ifdef NDEBUG
#define SPOT_ASSERT(condition, ...) static_cast<void>(sizeof(!(condition)))
#else
#define SPOT_ASSERT(condition, ...)                                                                                    \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            ::Spot::Detail::AssertionFailed(#condition, std::source_location::current(), __VA_ARGS__);                 \
        }                                                                                                              \
    } while (false)
#endif
