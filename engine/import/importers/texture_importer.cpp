#include "assets/asset_formats.h"
#include "import/importers/importer.h"

#include <utility>

namespace Spot::Detail {

bool ImportTexture(ImportContext& context) {
    auto image = Image::Load(context.GetSourcePath());
    if (!image) {
        return false;
    }

    BinaryWriter data;
    Encode(data, TextureFile{std::move(*image), context.GetMeta().texture.mipmaps});
    return context.WriteAsset(AssetType::Texture, data);
}

} // namespace Spot::Detail
