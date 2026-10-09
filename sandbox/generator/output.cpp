#include "output.h"

#include <spot.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace Generator {

bool WriteIfChanged(const std::filesystem::path& path, std::span<const std::byte> content, OutputCounts& counts) {
    std::ifstream existing(path, std::ios::binary);
    if (existing) {
        const std::vector<char> bytes{std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>()};
        if (std::ranges::equal(std::as_bytes(std::span(bytes)), content)) {
            ++counts.unchanged;
            return true;
        }
    }
    existing.close();

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
    if (!file) {
        Spot::Log::Error("Could not write '{}'", Spot::PathToUtf8(path));
        ++counts.failed;
        return false;
    }
    Spot::Log::Info("Wrote '{}'", Spot::PathToUtf8(path));
    ++counts.written;
    return true;
}

bool WriteIfChanged(const std::filesystem::path& path, std::string_view content, OutputCounts& counts) {
    return WriteIfChanged(path, std::as_bytes(std::span(content)), counts);
}

} // namespace Generator
