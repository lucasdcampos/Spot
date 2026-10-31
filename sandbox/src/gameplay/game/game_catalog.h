#pragma once

#include <spot.h>

#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

struct MapEntry {
    Spot::AssetId scene;
    std::string name;
    std::string description;
    std::string details;
    int order = 0;
    int maxPlayers = 16;
    Spot::AssetRef<Spot::Texture> preview;
    Spot::Vector3 previewPosition;
    Spot::Vector3 previewTarget;
    std::vector<Spot::AssetId> modes;
    Spot::AssetId defaultMode;

    bool SupportsMode(Spot::AssetId mode) const;
};

struct ModeEntry {
    Spot::AssetRef<Spot::Prefab> prefab;
    std::string name;
    std::string description;
    int order = 0;
};

// Discover imported scenes with MapInfo and prefabs whose active root has GameMode and NetworkObject, without
// starting their components.
// Entries have a stable order: their authored order, name, then asset id. No source files or importers are needed.
class MapCatalog {
public:
    void Refresh(Spot::AssetManager& assets, std::string_view directory = {});
    const std::vector<MapEntry>& GetEntries() const { return m_entries; }
    const MapEntry* Find(Spot::AssetId scene) const;

private:
    std::vector<MapEntry> m_entries;
};

class ModeCatalog {
public:
    void Refresh(Spot::AssetManager& assets, std::string_view directory = {});
    const std::vector<ModeEntry>& GetEntries() const { return m_entries; }
    const ModeEntry* Find(Spot::AssetId prefab) const;

private:
    std::vector<ModeEntry> m_entries;
};

} // namespace Gameplay
