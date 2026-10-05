#include "core/paths.h"

namespace Spot {

std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

std::filesystem::path Utf8ToPath(std::string_view utf8) {
    return std::u8string(utf8.begin(), utf8.end());
}

} // namespace Spot
