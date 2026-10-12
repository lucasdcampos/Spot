#include "assets/asset_formats.h"
#include "core/log.h"
#include "graphics/font_atlas.h"
#include "import/importers/importer.h"

#include <cmath>
#include <format>
#include <utility>

namespace Spot::Detail {

// The font file is kept whole, since text layout reads its kerning; glyphs are rasterized when the font loads. Each
// extra size becomes a sub-asset named after it, such as "48px".
bool ImportFont(ImportContext& context) {
    auto bytes = ReadFileBytes(context.GetSourcePath());
    const FontImportSettings& settings = context.GetMeta().font;
    if (!bytes || !(settings.pixelSize > 0.0f) || !FontAtlas::Create(*bytes, settings.pixelSize)) {
        return false;
    }

    for (const float size : settings.extraSizes) {
        if (!std::isfinite(size) || size < 1.0f || size > 256.0f) {
            Log::GetEngineLogger().Error("Font sizes must be finite and between 1 and 256 pixels, not {}", size);
            return false;
        }
        BinaryWriter data;
        Encode(data, FontFile{size, *bytes});
        const std::string name = std::format("{}px", size);
        if (!context.WriteSubAsset("size:" + name, AssetType::Font, name, data).IsValid()) {
            return false;
        }
    }

    BinaryWriter data;
    Encode(data, FontFile{settings.pixelSize, std::move(*bytes)});
    return context.WriteAsset(AssetType::Font, data);
}

} // namespace Spot::Detail
