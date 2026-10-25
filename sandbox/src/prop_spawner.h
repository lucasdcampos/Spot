#pragma once

#include "spawnable.h"

#include <spot.h>

#include <optional>
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
// cycles through the complete catalog; UndoList can take them back, and Clean Up removes them all. A prop chosen in the
// spawn menu is placed instead: a translucent preview of it follows the aim until Fire places it there or Aim cancels.
class PropSpawner final : public Spot::Component {
public:
    std::string folder = "prefabs/spawnables";
    float reach = 6.0f;
    // The preview's color, in its alpha how much it covers what is behind it.
    Spot::Color previewColor = {0.9f, 0.55f, 0.16f, 0.32f};

    // Spawns the prop of the list where the player looks; null when there is no such prop.
    Spot::Entity* Spawn(std::size_t index);
    // Shows the prop of the list where it would spawn, until it is placed or cancelled; false when there is none.
    bool BeginPlacement(std::size_t index);
    // Spawns the prop being placed where its preview is; null when nothing is being placed.
    Spot::Entity* ConfirmPlacement();
    void CancelPlacement();
    bool IsPlacing() const { return m_placing.has_value(); }
    std::optional<std::size_t> GetPlacingIndex() const { return m_placing; }
    // While placing, and after until Fire and Aim are released, so the press that places or cancels uses nothing else.
    bool HasClick() const { return IsPlacing() || m_holdingClick; }
    Spot::EntityId GetPreview() const { return m_preview; }
    void Cleanup();
    std::size_t GetSpawnedCount() const;
    std::span<const SpawnableItem> GetSpawnables() const { return m_spawnables; }
    // An explicit manager must outlive the spawner; defaults to the running application's manager.
    void RefreshSpawnables(Spot::AssetManager* assets = nullptr);
    bool IsSpawned(Spot::EntityId id) const;
    void TrackSpawned(Spot::EntityId id);
    void ClearTracked() { m_spawned.clear(); }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Placement {
        Spot::Vector3 position;
        // Turned to face where the player looks.
        Spot::Quaternion facing;
    };
    std::optional<Placement> FindPlacement(const SpawnableItem& item) const;
    void UpdatePreview();

    std::vector<SpawnableItem> m_spawnables;
    Spot::AssetManager* m_assets = nullptr;
    // What was spawned, oldest first, by id, since props may be destroyed by other means.
    std::vector<Spot::EntityId> m_spawned;
    // The prop Spawn makes next.
    std::size_t m_next = 0;
    std::optional<std::size_t> m_placing;
    Spot::EntityId m_preview = Spot::EntityId::Invalid;
    bool m_holdingClick = false;
};
