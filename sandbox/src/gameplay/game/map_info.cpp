#include "gameplay/game/map_info.h"

#include "gameplay/game/game_mode.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(MapInfo);

MapInfo* MapInfo::Get(Spot::Scene& scene) {
    return scene.FindComponent<MapInfo>();
}

bool MapInfo::SupportsMode(Spot::AssetId mode) const {
    return mode.IsValid() && (modes.empty() || std::ranges::find(modes, mode) != modes.end());
}

Spot::AssetId MapInfo::GetRestartScene() const {
    return restartScene.IsValid() ? restartScene : GetScene().GetSourceAsset();
}

GameMode* MapInfo::StartMode(const Spot::AssetRef<Spot::Prefab>& mode, Spot::AssetManager* assets) {
    m_error.clear();
    if (!Spot::Network::IsServer(GetScene())) {
        m_error = "Only the server can choose the game mode";
        return nullptr;
    }
    if (!mode) {
        m_error = "The game mode prefab is unavailable";
        return nullptr;
    }
    if (!SupportsMode(mode.GetId())) {
        m_error = "This map does not support the selected game mode";
        return nullptr;
    }
    if (GameMode::Get(GetScene())) {
        m_error = "A game mode is already running";
        return nullptr;
    }
    Spot::Entity* root = GetScene().Instantiate(mode, {.parent = &GetEntity(), .assets = assets});
    GameMode* rules = root ? root->GetComponent<GameMode>() : nullptr;
    if (!rules || !rules->IsActiveAndEnabled() || !root->HasComponent<Spot::NetworkObject>()) {
        m_error = "A game mode prefab needs an active GameMode and NetworkObject on its root";
        if (root) {
            root->Destroy();
        }
        return nullptr;
    }
    return rules;
}

void MapInfo::OnStart() {
    if (Spot::Network::IsServer(GetScene()) && !GameMode::Get(GetScene()) && defaultMode.GetId().IsValid()) {
        if (!StartMode(defaultMode)) {
            Spot::Log::Error("Could not start map '{}': {}", name, m_error);
        }
    }
}

void MapInfo::Serialize(Spot::Archive& archive) {
    archive.Field("name", name);
    archive.Field("description", description);
    archive.Field("details", details);
    archive.Field("order", order);
    archive.Field("maxPlayers", maxPlayers, {.min = 1.0f, .max = 128.0f});
    archive.Field("preview", preview);
    archive.Field("previewPosition", previewPosition);
    archive.Field("previewTarget", previewTarget);
    archive.Field("modes", modes, {.assetType = Spot::AssetType::Prefab});
    archive.Field("defaultMode", defaultMode);
    archive.Field("checkpoint", checkpoint);
    archive.Field("restartScene", restartScene, {.assetType = Spot::AssetType::Scene});
    archive.Field("introTime", introTime, {.min = 0.0f, .max = 60.0f});
}

} // namespace Gameplay
