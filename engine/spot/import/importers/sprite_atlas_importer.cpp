#include "spot/assets/asset_formats.h"
#include "spot/core/log.h"
#include "spot/graphics/sprite_atlas.h"
#include "spot/import/importers/importer.h"

namespace Spot::Detail {
bool ImportSpriteAtlas(ImportContext& context) {
    const auto bytes = ReadFileBytes(context.GetSourcePath());
    const auto atlas =
        bytes
            ? SpriteAtlas::LoadFromString(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()))
            : std::nullopt;
    if (!atlas) {
        Log::GetEngineLogger().Error("Invalid sprite atlas source");
        return false;
    }
    BinaryWriter writer;
    EncodeScene(writer, atlas->SaveToString());
    return context.WriteAsset(AssetType::SpriteAtlas, writer);
}
} // namespace Spot::Detail
