#include "assets/asset_formats.h"
#include "import/importers/importer.h"
#include "scene/prefab.h"

#include <string_view>

namespace Spot::Detail {

bool ImportPrefab(ImportContext& context) {
    const auto bytes = ReadFileBytes(context.GetSourcePath());
    if (!bytes) {
        return false;
    }
    const auto prefab =
        Prefab::FromString(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    if (!prefab) {
        return false;
    }
    BinaryWriter data;
    EncodeScene(data, prefab->GetEntityText());
    return context.WriteAsset(AssetType::Prefab, data);
}

} // namespace Spot::Detail
