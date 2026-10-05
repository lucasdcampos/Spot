#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace Spot {

// Pixels in CPU memory, always 8-bit RGBA with rows from top to bottom.
class Image {
public:
    Image(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> pixels);

    // Reads PNG, JPEG, BMP, TGA and other common formats. Returns nothing, after logging why, on failure.
    static std::optional<Image> Load(const std::filesystem::path& path);
    static std::optional<Image> LoadFromMemory(std::span<const std::byte> data);

    std::uint32_t GetWidth() const { return m_width; }
    std::uint32_t GetHeight() const { return m_height; }
    std::span<const std::uint8_t> GetPixels() const { return m_pixels; }

private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::vector<std::uint8_t> m_pixels;
};

} // namespace Spot
