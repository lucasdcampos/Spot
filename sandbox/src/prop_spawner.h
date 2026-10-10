#pragma once

#include "spawnable.h"

#include <spot.h>

#include <string>
#include <vector>

struct SpawnableItem {
    Spot::AssetRef<Spot::Prefab> prefab;
    std::string name;
    Spawnable::Category category = Spawnable::Category::Prop;
    int order = 100;
    float clearance = 0.55f;
};

// Discovers spawnable prefabs from the imported manifest. Spawn 1 to Spawn 8 choose the first entries, and Spawn
// cycles through the complete catalog; UndoList can take them back, and Clean Up removes them all.
class PropSpawner final : public Spot::Component {
public:
    std::string folder = "prefabs/spawnables";
    float reach = 6.0f;

    // Spawns the prop of the list where the player looks; null when there is no such prop.
    Spot::Entity* Spawn(std::size_t index);
    void Cleanup();
    std::size_t GetSpawnedCount() const;
    std::span<const SpawnableItem> GetSpawnables() const { return m_spawnables; }
    // An explicit manager must outlive the spawner; defaults to the running application's manager.
    void RefreshSpawnables(Spot::AssetManager* assets = nullptr);

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    std::vector<SpawnableItem> m_spawnables;
    Spot::AssetManager* m_assets = nullptr;
    // What was spawned, oldest first, by id, since props may be destroyed by other means.
    std::vector<Spot::EntityId> m_spawned;
    // The prop Spawn makes next.
    std::size_t m_next = 0;
};
