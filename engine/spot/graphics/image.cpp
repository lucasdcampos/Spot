#include "spot/graphics/image.h"

#include "spot/core/debug/assert.h"
#include "spot/core/debug/log.h"
#include "spot/core/io/paths.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace Spot {

Image::Image(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> pixels)
    : m_width(width), m_height(height), m_pixels(std::move(pixels)) {
    SPOT_ASSERT(m_pixels.size() == static_cast<std::size_t>(width) * height * 4,
                "A {}x{} RGBA image needs {} bytes, not {}", width, height,
                static_cast<std::size_t>(width) * height * 4, m_pixels.size());
}

namespace {

template <typename Shade>
Image CreateGrayImage(std::uint32_t size, Shade shade) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const std::uint8_t value = shade(x, y);
            auto* pixel = &pixels[(static_cast<std::size_t>(y) * size + x) * 4];
            pixel[0] = value;
            pixel[1] = value;
            pixel[2] = value;
            pixel[3] = 255;
        }
    }
    return Image(size, size, std::move(pixels));
}

} // namespace

Image Image::CreateGrid(std::uint32_t size) {
    SPOT_ASSERT(size >= 8, "A grid needs at least 8 pixels per side, not {}", size);
    const std::uint32_t line = std::max(size / 128, 1u);
    const std::uint32_t quarter = size / 4;
    return CreateGrayImage(size, [=](std::uint32_t x, std::uint32_t y) -> std::uint8_t {
        // The border runs along two edges only, since the neighboring squares draw the other two.
        if (x < line || y < line) {
            return 96;
        }
        if (x % quarter < line || y % quarter < line) {
            return 160;
        }
        return 210;
    });
}

Image Image::CreateChecker(std::uint32_t size) {
    SPOT_ASSERT(size >= 8, "A checker needs at least 8 pixels per side, not {}", size);
    const std::uint32_t square = size / 8;
    return CreateGrayImage(size, [=](std::uint32_t x, std::uint32_t y) -> std::uint8_t {
        return (x / square + y / square) % 2 == 0 ? 230 : 120;
    });
}

std::optional<Image> Image::Load(const std::filesystem::path& path) {
    // Read through std::ifstream rather than stb's own file access, which mishandles non-ASCII paths on Windows.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Log::GetEngineLogger().Error("Could not open image '{}'", PathToUtf8(path));
        return std::nullopt;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    auto image = LoadFromMemory(std::as_bytes(std::span(bytes)));
    if (!image) {
        Log::GetEngineLogger().Error("Could not decode image '{}'", PathToUtf8(path));
    }
    return image;
}

std::vector<std::byte> Image::EncodePng() const {
    std::vector<std::byte> encoded;
    const auto append = [](void* context, void* data, int size) {
        auto& bytes = *static_cast<std::vector<std::byte>*>(context);
        const auto* begin = static_cast<const std::byte*>(data);
        bytes.insert(bytes.end(), begin, begin + size);
    };
    stbi_write_png_to_func(append, &encoded, static_cast<int>(m_width), static_cast<int>(m_height), 4, m_pixels.data(),
                           static_cast<int>(m_width * 4));
    return encoded;
}

bool Image::SavePng(const std::filesystem::path& path) const {
    const std::vector<std::byte> encoded = EncodePng();
    // Written through std::ofstream, like images are read, so non-ASCII paths work on Windows.
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (encoded.empty() || !file) {
        Log::GetEngineLogger().Error("Could not write image '{}'", PathToUtf8(path));
        return false;
    }
    return true;
}

std::optional<Image> Image::LoadFromMemory(std::span<const std::byte> data) {
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        Log::GetEngineLogger().Error("Image data is too large to decode");
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                            static_cast<int>(data.size()), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        Log::GetEngineLogger().Error("Failed to decode image: {}", stbi_failure_reason());
        return std::nullopt;
    }

    const std::size_t size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<std::uint8_t> copy(pixels, pixels + size);
    stbi_image_free(pixels);
    return Image(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), std::move(copy));
}

} // namespace Spot
