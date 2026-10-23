#include "spot/assets/asset_formats.h"
#include "spot/import/importers/importer.h"

namespace Spot::Detail {

bool ImportMaterial(ImportContext& context) {
    const auto material = MaterialSource::LoadFromFile(context.GetSourcePath());
    if (!material) {
        return false;
    }

    BinaryWriter data;
    Encode(data, *material);
    return context.WriteAsset(AssetType::Material, data);
}

} // namespace Spot::Detail
