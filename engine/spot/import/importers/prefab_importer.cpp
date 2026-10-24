#include "spot/assets/asset_formats.h"
#include "spot/core/debug/log.h"
#include "spot/import/importers/importer.h"
#include "spot/scene/prefab.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Spot::Detail {

namespace {

std::optional<Prefab> ReadPrefab(const std::filesystem::path& path) {
    const auto bytes = ReadFileBytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    return Prefab::FromString(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

} // namespace

// Nested prefabs are flattened from their sources, so the order prefabs import in does not matter, and each of them
// is a dependency, so changing one imports the prefabs nesting it again.
bool ImportPrefab(ImportContext& context) {
    const auto prefab = ReadPrefab(context.GetSourcePath());
    if (!prefab) {
        return false;
    }
    std::vector<AssetId> nesting{context.GetMeta().id};
    std::unordered_map<AssetId, AssetRef<Prefab>> flattened;
    Prefab::NestedPrefabs find = [&](AssetId id) -> AssetRef<Prefab> {
        if (const auto found = flattened.find(id); found != flattened.end()) {
            return found->second;
        }
        if (nesting.size() >= 64 || std::ranges::find(nesting, id) != nesting.end()) {
            Log::GetEngineLogger().Error("Prefab {} nests itself, or prefabs are nested deeper than 64", id.ToString());
            return {};
        }
        const auto path = context.FindSource(id);
        if (path.empty()) {
            return {};
        }
        context.AddDependency(path);
        const auto nested = ReadPrefab(path);
        if (!nested) {
            return {};
        }
        nesting.push_back(id);
        auto flat = nested->Flatten(find);
        nesting.pop_back();
        if (!flat) {
            return {};
        }
        return flattened[id] = AssetRef<Prefab>(id, std::make_shared<const Prefab>(std::move(*flat)));
    };
    const auto flat = prefab->Flatten(find);
    if (!flat) {
        return false;
    }
    BinaryWriter data;
    EncodeScene(data, flat->GetEntityText());
    return context.WriteAsset(AssetType::Prefab, data);
}

} // namespace Spot::Detail
