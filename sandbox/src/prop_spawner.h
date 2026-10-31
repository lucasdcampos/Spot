#pragma once

#include "spawnable.h"

#include "gameplay/core/input_context.h"
#include "gameplay/core/player_input.h"
#include "gameplay/persistence/world_save.h"

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
// While placing, and after until Fire and Aim are released, so the press that places or cancels uses nothing else, it
// takes them from everything else, and the player is "Placing"; getting into a vehicle, or dying, cancels it. What it
// spawns is Persistent, so saves keep it, and tagged as spawned, so it is tracked again after a save loads. On a
// machine that does not decide its pawn, spawning and cleaning up are requests the server carries out, where the
// player looked, if that is within reach.
class PropSpawner final : public Spot::Component, public Gameplay::SaveListener, public Gameplay::PlayerInputListener {
public:
    static constexpr std::string_view SpawnedTag = "Spawned";

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
    // The actions are read as it updates, so the input itself does nothing.
    void OnPlayerInput(Gameplay::PlayerController& /*player*/) override {}
    // Saving takes the requests, which are sent once.
    void SerializeIntent(Spot::Archive& archive) override;
    void OnAfterLoad(std::span<Spot::Entity* const> roots) override;
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
    struct Request {
        int index = 0;
        Spot::Vector3 position;
        Spot::Quaternion facing;

        void Serialize(Spot::Archive& archive);
    };
    Spot::Entity* SpawnAt(std::size_t index, const Placement& placement);
    std::optional<Placement> FindPlacement(const SpawnableItem& item) const;
    void UpdatePreview();
    // Tells the pawn it is placing, for the crosshair to mark where.
    void SetPlacing(bool placing);

    std::vector<SpawnableItem> m_spawnables;
    Spot::AssetManager* m_assets = nullptr;
    // What was spawned, oldest first, by id, since props may be destroyed by other means.
    std::vector<Spot::EntityId> m_spawned;
    // The prop Spawn makes next.
    std::size_t m_next = 0;
    std::optional<std::size_t> m_placing;
    Spot::EntityId m_preview = Spot::EntityId::Invalid;
    Gameplay::InputContext m_input{{.name = "Spawn",
                                    .actions = {"Spawn 1", "Spawn 2", "Spawn 3", "Spawn 4", "Spawn 5", "Spawn 6",
                                                "Spawn 7", "Spawn 8", "Spawn", "Clean Up"}}};
    Gameplay::InputContext m_place{{.name = "Place", .priority = 40, .actions = {"Fire", "Aim"}}};
    bool m_holdingClick = false;
    std::vector<Request> m_requests;
    bool m_cleanupRequested = false;
};
