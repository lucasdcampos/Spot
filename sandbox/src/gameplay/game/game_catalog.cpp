#include "gameplay/game/game_catalog.h"

#include "gameplay/game/game_mode.h"
#include "gameplay/game/map_info.h"

#include <algorithm>

namespace Gameplay {

bool MapEntry::SupportsMode(Spot::AssetId mode) const {
    return mode.IsValid() && (modes.empty() || std::ranges::find(modes, mode) != modes.end());
}

void MapCatalog::Refresh(Spot::AssetManager& assets, std::string_view directory) {
    m_entries.clear();
    for (const Spot::AssetId id : assets.GetAssetIds(Spot::AssetType::Scene, directory)) {
        const auto scene = Spot::InstantiateScene(assets, id);
        const MapInfo* info = scene ? MapInfo::Get(*scene) : nullptr;
        if (info && !info->name.empty()) {
            m_entries.push_back({.scene = id,
                                 .name = info->name,
                                 .description = info->description,
                                 .details = info->details,
                                 .order = info->order,
                                 .maxPlayers = info->maxPlayers,
                                 .preview = info->preview,
                                 .previewPosition = info->previewPosition,
                                 .previewTarget = info->previewTarget,
                                 .modes = info->modes,
                                 .defaultMode = info->defaultMode.GetId()});
        }
    }
    std::ranges::sort(m_entries, [](const MapEntry& a, const MapEntry& b) {
        if (a.order != b.order) {
            return a.order < b.order;
        }
        return a.name != b.name ? a.name < b.name : a.scene.ToString() < b.scene.ToString();
    });
}

const MapEntry* MapCatalog::Find(Spot::AssetId scene) const {
    const auto found = std::ranges::find(m_entries, scene, &MapEntry::scene);
    return found != m_entries.end() ? &*found : nullptr;
}

void ModeCatalog::Refresh(Spot::AssetManager& assets, std::string_view directory) {
    m_entries.clear();
    for (const Spot::AssetId id : assets.GetAssetIds(Spot::AssetType::Prefab, directory)) {
        const auto prefab = assets.Load<Spot::Prefab>(id);
        Spot::Scene preview;
        const Spot::Entity* root = prefab ? preview.Instantiate(prefab, {.assets = &assets}) : nullptr;
        const GameMode* mode = root ? root->GetComponent<GameMode>() : nullptr;
        if (mode && mode->IsActiveAndEnabled() && root->HasComponent<Spot::NetworkObject>() && !mode->name.empty()) {
            m_entries.push_back({prefab, mode->name, mode->description, mode->order});
        }
    }
    std::ranges::sort(m_entries, [](const ModeEntry& a, const ModeEntry& b) {
        if (a.order != b.order) {
            return a.order < b.order;
        }
        return a.name != b.name ? a.name < b.name : a.prefab.GetId().ToString() < b.prefab.GetId().ToString();
    });
}

const ModeEntry* ModeCatalog::Find(Spot::AssetId prefab) const {
    const auto found =
        std::ranges::find_if(m_entries, [prefab](const ModeEntry& entry) { return entry.prefab.GetId() == prefab; });
    return found != m_entries.end() ? &*found : nullptr;
}

} // namespace Gameplay
