#include "spot/platform/shell.h"

#include <format>

namespace Spot::Shell {

std::string ToFileUrl(const std::filesystem::path& path) {
    const std::u8string generic = path.generic_u8string();
    const std::string utf8(generic.begin(), generic.end());
    std::string url = "file://";
    // Windows paths start with a drive letter, which URLs put after a third slash.
    if (!utf8.starts_with('/')) {
        url += '/';
    }
    for (const char c : utf8) {
        const auto byte = static_cast<unsigned char>(c);
        const bool kept = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                          (byte >= '0' && byte <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' ||
                          c == ':';
        url += kept ? std::string(1, c) : std::format("%{:02X}", byte);
    }
    return url;
}

} // namespace Spot::Shell
