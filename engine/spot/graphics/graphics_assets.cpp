#include "spot/graphics/graphics_assets.h"
#include "spot/assets/asset_manager.h"
#include "spot/assets/built_in_assets.h"
#include "spot/core/log.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/texture_asset_format.h"

#include <algorithm>
#include <utility>

namespace Spot {
void RegisterGraphicsAssets(AssetManager& assets, GraphicsDevice* device) {
    const bool bc1 = device && device->SupportsSampling(TextureFormat::Bc1);
    const bool bc1Srgb = device && device->SupportsSampling(TextureFormat::Bc1Srgb);
    const bool bc3 = device && device->SupportsSampling(TextureFormat::Bc3);
    const bool bc3Srgb = device && device->SupportsSampling(TextureFormat::Bc3Srgb);
    assets.RegisterLoader(
        AssetType::Texture,
        MakeAssetLoader<Detail::TextureFile>(
            [=](AssetId id, std::span<const std::byte> data) -> std::optional<Detail::TextureFile> {
                if (id == BuiltInAssets::GridTexture || id == BuiltInAssets::CheckerTexture) {
                    return Detail::TextureFile{.image = id == BuiltInAssets::GridTexture ? Image::CreateGrid()
                                                                                         : Image::CreateChecker(),
                                               .srgb = false};
                }
                auto texture = Detail::DecodeTexture(data);
                if (texture && texture->compressedFormat) {
                    const auto format = *texture->compressedFormat;
                    const bool supported =
                        (format == TextureFormat::Bc1 && bc1) || (format == TextureFormat::Bc1Srgb && bc1Srgb) ||
                        (format == TextureFormat::Bc3 && bc3) || (format == TextureFormat::Bc3Srgb && bc3Srgb);
                    if (!supported) {
                        texture->image = Image(
                            texture->width, texture->height,
                            Detail::DecompressBlocks(format, texture->width, texture->height, texture->levels[0]));
                        texture->compressedFormat.reset();
                    }
                }
                return texture;
            },
            [device](AssetManager&, AssetId, Detail::TextureFile& texture) -> std::shared_ptr<const void> {
                if (!device) {
                    Log::GetEngineLogger().Error("Texture assets need a graphics device");
                    return nullptr;
                }
                if (texture.compressedFormat) {
                    auto result = device->CreateTexture({.width = texture.width,
                                                         .height = texture.height,
                                                         .format = *texture.compressedFormat,
                                                         .mipmaps = texture.levels.size() > 1});
                    if (!result)
                        return nullptr;
                    for (std::uint32_t i = 0; i < texture.levels.size() && i < result->GetMipLevelCount(); ++i)
                        device->UploadToTextureLevel(*result, i, texture.levels[i]);
                    return std::shared_ptr<Texture>(std::move(result));
                }
                return std::shared_ptr<Texture>(device->CreateTexture(
                    texture.image, texture.mipmaps, texture.srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8));
            }));
}
} // namespace Spot
