#include "scene/scene_assets.h"

#include "assets/asset_formats.h"
#include "assets/asset_manager.h"
#include "assets/assets.h"
#include "core/log.h"
#include "scene/prefab.h"
#include "scene/scene.h"

#include <mutex>

namespace Spot {

namespace Detail {

void RegisterSceneAssetLoaders() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        AssetManager::RegisterLoader(
            AssetType::Prefab,
            {.load = [](AssetManager&, AssetId, std::span<const std::byte> data) -> std::shared_ptr<const void> {
                 const auto text = DecodeScene(data);
                 auto prefab = text ? Prefab::FromString(*text) : std::nullopt;
                 return prefab ? std::make_shared<const Prefab>(std::move(*prefab)) : nullptr;
             },
             .dependencies =
                 [](const void* asset) {
                     const auto dependencies = static_cast<const Prefab*>(asset)->GetDependencies();
                     return std::vector<AssetId>(dependencies.begin(), dependencies.end());
                 }});
    });
}

} // namespace Detail

namespace {

// Programs that link the scene layer load its assets from the start, scenes made or not.
[[maybe_unused]] const bool s_loadersRegistered = (Detail::RegisterSceneAssetLoaders(), true);

} // namespace

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
