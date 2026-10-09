#include "assets/asset_formats.h"
#include "core/log.h"
#include "import/importers/importer.h"

#include <nlohmann/json.hpp>

#include <string_view>

namespace Spot::Detail {

// Scenes are kept as text. Their contents are only checked in full when they are instantiated, since their
// components may only be registered by then.
bool ImportScene(ImportContext& context) {
    const auto bytes = ReadFileBytes(context.GetSourcePath());
    if (!bytes) {
        return false;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    if (!nlohmann::json::parse(text.begin(), text.end(), nullptr, false).is_object()) {
        Log::GetEngineLogger().Error("The scene is not a JSON object");
        return false;
    }

    BinaryWriter data;
    EncodeScene(data, text);
    return context.WriteAsset(AssetType::Scene, data);
}

} // namespace Spot::Detail
