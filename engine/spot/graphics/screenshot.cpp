#include "spot/graphics/screenshot.h"

#include "spot/core/debug/log.h"
#include "spot/graphics/graphics_device.h"

#include <algorithm>
#include <utility>

namespace Spot {

bool SaveScreenshot(GraphicsDevice& device, const Texture& target, const std::filesystem::path& path) {
    const auto format = target.GetFormat();
    const bool bgra = format == TextureFormat::Bgra8 || format == TextureFormat::Bgra8Srgb;
    if ((!bgra && format != TextureFormat::Rgba8 && format != TextureFormat::Rgba8Srgb) ||
        target.GetSampleCount() != 1 || target.GetType() != TextureType::Texture2D || target.GetLayerCount() != 1) {
        Log::GetEngineLogger().Warning("Screenshots require a single-sample RGBA/BGRA 2D texture");
        return false;
    }
    auto pixels = device.DownloadTexture(target);
    const auto expected = static_cast<std::size_t>(target.GetWidth()) * target.GetHeight() * 4;
    if (pixels.size() != expected) {
        return false;
    }
    if (bgra) {
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);
        }
    }
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    if (error) {
        return false;
    }
    return Image(target.GetWidth(), target.GetHeight(), std::move(pixels)).SavePng(path);
}

} // namespace Spot
