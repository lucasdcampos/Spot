#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

namespace Generator {

// What happened to the files written so far, for the generator's summary.
struct OutputCounts {
    int written = 0;
    int unchanged = 0;
    int failed = 0;
};

// Writes the file only when its content changed, creating its folder, so files that come out the same keep their
// time and are not imported again. Logs and counts what it did.
bool WriteIfChanged(const std::filesystem::path& path, std::span<const std::byte> content, OutputCounts& counts);
bool WriteIfChanged(const std::filesystem::path& path, std::string_view content, OutputCounts& counts);

} // namespace Generator
