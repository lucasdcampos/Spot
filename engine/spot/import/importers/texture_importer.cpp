#include "spot/assets/asset_formats.h"
#include "spot/graphics/texture_asset_format.h"
#include "spot/import/importers/importer.h"
#include "spot/import/importers/texture_compression.h"

#include <utility>

namespace Spot::Detail {

bool ImportTexture(ImportContext& context) {
    auto image = Image::Load(context.GetSourcePath());
    if (!image) {
        return false;
    }

    const TextureImportSettings& settings = context.GetMeta().texture;
    BinaryWriter data;
    if (settings.compress && CanCompressTexture(*image, settings.mipmaps)) {
        Encode(data, CompressTexture(*image, settings.srgb, settings.keepAlphaCoverage));
    } else {
        Encode(data, TextureFile{std::move(*image), settings.mipmaps, settings.srgb});
    }
    return context.WriteAsset(AssetType::Texture, data);
}

} // namespace Spot::Detail
