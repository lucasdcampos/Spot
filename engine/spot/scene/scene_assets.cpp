#include "spot/scene/scene_assets.h"

#include "spot/assets/asset_formats.h"
#include "spot/assets/asset_manager.h"
#include "spot/assets/assets.h"
#include "spot/core/debug/log.h"
#include "spot/core/jobs/job_system.h"
#include "spot/scene/prefab.h"
#include "spot/scene/scene.h"

#include <array>
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

struct Scene::PendingChunk {
    AssetManager* assets;
    AssetId id;
    std::shared_ptr<Detail::PreloadTask> preload;
    std::shared_ptr<ChunkLoad::State> state;
    JobCounter reading;
    std::optional<std::string> text;
};

Entity* ChunkLoad::GetRoot() const {
    return m_state && m_state->scene ? m_state->scene->GetEntity(m_state->root) : nullptr;
}

ChunkLoad Scene::LoadChunkAsync(AssetManager& assets, AssetId scene) {
    auto state = std::make_shared<ChunkLoad::State>();
    state->scene = this;
    auto pending = std::make_shared<PendingChunk>();
    pending->assets = &assets;
    pending->id = scene;
    pending->state = state;
    pending->preload = assets.StartPreload(std::array{scene});
    JobSystem::GetShared().Run(
        [pending] {
            const auto data = pending->assets->ReadImportedData(pending->id, AssetType::Scene);
            pending->text = data ? Detail::DecodeScene(*data) : std::nullopt;
        },
        &pending->reading);
    m_chunkLoads.push_back(pending);
    std::erase_if(m_chunkStates, [](const auto& weak) { return weak.expired(); });
    m_chunkStates.push_back(state);
    return ChunkLoad(state);
}

void Scene::FinishChunkLoads() {
    // Keep new requests separate from the batch, since component creation may request another chunk.
    auto loads = std::exchange(m_chunkLoads, {});
    for (const auto& pending : loads) {
        if (!pending->reading.IsDone() || !AssetManager::IsReady(*pending->preload)) {
            m_chunkLoads.push_back(pending);
            continue;
        }
        JobSystem::GetShared().Wait(pending->reading);
        const auto preloaded = pending->assets->FinishPreload(*pending->preload);
        if (pending->text) {
            Entity& root = CreateEntity(std::string(pending->assets->GetPath(pending->id)));
            const auto entities = Instantiate(*pending->text, {.parent = &root, .assets = pending->assets});
            // Empty scenes are valid chunks too; LoadFromString validates their format without adding entities.
            if (!entities.empty()) {
                pending->state->root = root.GetId();
            } else {
                Scene validation;
                if (validation.LoadFromString(*pending->text, pending->assets))
                    pending->state->root = root.GetId();
                else
                    root.Destroy();
            }
        }
        if (pending->state->root == EntityId::Invalid)
            Log::GetEngineLogger().Error("Could not load scene chunk {}", pending->id.ToString());
        pending->state->done = true;
    }
}

void Scene::CancelChunkLoads() {
    for (const auto& pending : m_chunkLoads)
        JobSystem::GetShared().Wait(pending->reading);
    m_chunkLoads.clear();
    for (const auto& weak : m_chunkStates) {
        if (const auto state = weak.lock()) {
            state->scene = nullptr;
            state->done = true;
        }
    }
    m_chunkStates.clear();
}

std::unique_ptr<Scene> InstantiateScene(AssetManager& assets, AssetId id) {
    const auto data = assets.ReadImportedData(id, AssetType::Scene);
    const auto text = data ? Detail::DecodeScene(*data) : std::nullopt;
    auto scene = std::make_unique<Scene>();
    if (!text || !scene->LoadFromString(*text, &assets)) {
        Log::GetEngineLogger().Error("Could not instantiate scene {} from '{}'", id.ToString(), assets.GetPath(id));
        return nullptr;
    }
    scene->SetSourceAsset(id);
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
