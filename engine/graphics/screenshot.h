#pragma once

#include <filesystem>

namespace Spot {
class GraphicsDevice;
class Texture;

// After submission, reads a single-sample 2D RGBA/BGRA target and writes a PNG. Creates parent directories.
bool SaveScreenshot(GraphicsDevice& device, const Texture& target, const std::filesystem::path& path);
} // namespace Spot
