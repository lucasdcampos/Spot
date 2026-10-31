#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class GameMode;

// Data authored by the map, independently of its rules. A server starts its default mode on the first update;
// clients receive the mode prefab from that server. Loading a map for a catalog or preview never starts a game.
class MapInfo final : public Spot::Component {
public:
    std::string name;
    std::string description;
    std::string details;
    int order = 0;
    int maxPlayers = 16;
    Spot::AssetRef<Spot::Texture> preview;
    Spot::Vector3 previewPosition = {24.0f, 18.0f, 28.0f};
    Spot::Vector3 previewTarget;
    // An empty list supports any mode; otherwise these are mode prefab asset ids.
    std::vector<Spot::AssetId> modes;
    Spot::AssetRef<Spot::Prefab> defaultMode;
    std::string checkpoint = "world.json";
    // This map's asset when unset.
    Spot::AssetId restartScene;
    float introTime = 0.0f;

    static MapInfo* Get(Spot::Scene& scene);
    bool SupportsMode(Spot::AssetId mode) const;
    Spot::AssetId GetRestartScene() const;
    // Select before the scene starts, or leave defaultMode to start automatically. A running mode cannot be replaced.
    GameMode* StartMode(const Spot::AssetRef<Spot::Prefab>& mode, Spot::AssetManager* assets = nullptr);
    const std::string& GetLastError() const { return m_error; }

    void Serialize(Spot::Archive& archive) override;

protected:
    void OnStart() override;

private:
    std::string m_error;
};

} // namespace Gameplay
