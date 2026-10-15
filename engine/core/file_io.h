#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Spot::Detail {

// The whole file; nothing when it cannot be read.
std::optional<std::vector<std::byte>> ReadFileBytes(const std::filesystem::path& path);

} // namespace Spot::Detail
