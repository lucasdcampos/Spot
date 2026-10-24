#include "spot/scene/scene_assets.h"

#include "spot/assets/asset_formats.h"
#include "spot/assets/asset_manager.h"
#include "spot/assets/assets.h"
#include "spot/core/log.h"
#include "spot/scene/prefab.h"
#include "spot/scene/scene.h"

#include <utility>

namespace Spot {

void RegisterSceneAssets(AssetManager& assets) {
    auto loader = MakeAssetLoader<Prefab>(
        [](AssetId, std::span<const std::byte> data) -> std::optional<Prefab> {
            const auto text = Detail::DecodeScene(data);
            return text ? Prefab::FromString(*text) : std::nullopt;
        },
        [](AssetManager&, AssetId, Prefab& prefab) { return std::make_shared<const Prefab>(std::move(prefab)); });
    loader.textReferences = true;
    loader.dependencies = [](const void* data) {
        const auto dependencies = static_cast<const Prefab*>(data)->GetDependencies();
        return std::vector<AssetId>(dependencies.begin(), dependencies.end());
    };
    assets.RegisterLoader(AssetType::Prefab, std::move(loader));
}

std::unique_ptr<Scene> InstantiateScene(AssetManager& assets, AssetId id) {
    const auto data = assets.ReadImportedData(id, AssetType::Scene);
    const auto text = data ? Detail::DecodeScene(*data) : std::nullopt;
    auto scene = std::make_unique<Scene>();
    if (!text || !scene->LoadFromString(*text, &assets)) {
        Log::GetEngineLogger().Error("Could not instantiate scene {} from '{}'", id.ToString(), assets.GetPath(id));
        return nullptr;
    }
    return scene;
}

std::unique_ptr<Scene> InstantiateScene(AssetManager& assets, std::string_view path) {
    const auto id = assets.FindByPath(path);
    if (!id) {
        Log::GetEngineLogger().Error("No asset at '{}'", path);
        return nullptr;
    }
    return InstantiateScene(assets, *id);
}

std::unique_ptr<Scene> Assets::InstantiateScene(std::string_view path) {
    AssetManager* manager = GetManager();
    if (!manager) {
        Log::GetEngineLogger().Error("Scene '{}' cannot be instantiated without a running application", path);
        return nullptr;
    }
    return Spot::InstantiateScene(*manager, path);
}

} // namespace Spot
