#include "core/diagnostics.h"

#include <string_view>

#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>
#include <cxxabi.h>
#include <memory>
#endif

namespace Spot::Detail {

std::string DescribeException(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& exception) {
        return exception.what();
    } catch (...) {
        return "an exception that is not a std::exception";
    }
}

std::string GetTypeName(const std::type_info& type) {
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    const std::unique_ptr<char, decltype(&std::free)> demangled(
        abi::__cxa_demangle(type.name(), nullptr, nullptr, &status), &std::free);
    return status == 0 && demangled ? std::string(demangled.get()) : std::string(type.name());
#else
    // MSVC's names are readable already, with the kind of type in front.
    std::string_view name = type.name();
    for (const std::string_view prefix : {"class ", "struct "}) {
        if (name.starts_with(prefix)) {
            name.remove_prefix(prefix.size());
        }
    }
    return std::string(name);
#endif
}

} // namespace Spot::Detail
