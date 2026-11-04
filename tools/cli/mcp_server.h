#pragma once

#include <filesystem>
#include <optional>

namespace Spot {

int RunMcpServer(const std::optional<std::filesystem::path>& projectPath = std::nullopt);

} // namespace Spot
