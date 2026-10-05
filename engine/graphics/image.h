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

    // Debug textures for prototyping, in grays that materials can tint. The grid is one square with a dark border,
    // split into four by fainter lines, so repeated it marks units and quarters. The checker has 8 by 8 squares, which
    // shows how UVs stretch.
    static Image CreateGrid(std::uint32_t size = 256);
    static Image CreateChecker(std::uint32_t size = 256);

    std::uint32_t GetWidth() const { return m_width; }
    std::uint32_t GetHeight() const { return m_height; }
    std::span<const std::uint8_t> GetPixels() const { return m_pixels; }

    // Encodes the image as PNG, such as for screenshots.
    std::vector<std::byte> EncodePng() const;
    // Returns false, after logging why, when the file cannot be written.
    bool SavePng(const std::filesystem::path& path) const;

private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::vector<std::uint8_t> m_pixels;
};

} // namespace Spot
