#include "graphics/image.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/paths.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

namespace Spot {

Image::Image(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> pixels)
    : m_width(width), m_height(height), m_pixels(std::move(pixels)) {
    SPOT_ASSERT(m_pixels.size() == static_cast<std::size_t>(width) * height * 4,
                "A {}x{} RGBA image needs {} bytes, not {}", width, height,
                static_cast<std::size_t>(width) * height * 4, m_pixels.size());
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
