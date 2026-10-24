#include "spot/assets/script_assets.h"
#include "spot/assets/asset_file.h"
#include "spot/assets/asset_manager.h"
#include "spot/assets/script_source.h"

#include <algorithm>
#include <utility>

namespace Spot {
void RegisterScriptAssets(AssetManager& assets) {
    assets.RegisterLoader(
        AssetType::Script,
        MakeAssetLoader<std::string>(
            [](AssetId, std::span<const std::byte> data) -> std::optional<std::string> {
                Detail::BinaryReader reader(data);
                auto source = reader.ReadString();
                return reader.IsValid() && reader.IsAtEnd() ? std::optional(std::move(source)) : std::nullopt;
            },
            [](AssetManager& assets, AssetId id, std::string& source) {
                return std::make_shared<const ScriptSource>(std::move(source), std::string(assets.GetPath(id)));
            }));
}
} // namespace Spot
