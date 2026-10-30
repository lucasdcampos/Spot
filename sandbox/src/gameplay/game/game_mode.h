#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class Pawn;
class PlayerController;
class SpawnPoint;

// The scene's rules for its local players. Gives a player without a pawn one made from playerPrefab at a spawn point,
// remembers their starting pawns, possesses them, and returns them after death or falling out of the world: to a
// spawn point, the one farthest from the other players, or to where they started when the map has none. Games
// override RespawnPlayer() and ChooseSpawn() for other rules.
class GameMode : public Spot::Component {
public:
    float respawnTime = 4.0f;
    float fallHeight = -20.0f;
    Spot::AssetId restartScene;
    // The pawn of a player who has none.
    Spot::AssetRef<Spot::Prefab> playerPrefab;
    // The spawn points it uses: those of this tag and those without one; all of them when empty.
    std::string spawnTag;

    static GameMode* Get(Spot::Scene& scene);
    float GetRespawnTimeLeft(const PlayerController& player) const;
    void Restart();

protected:
    virtual void RespawnPlayer(PlayerController& player, Pawn& pawn, Spot::Vector3 start);
    // The spawn point the player appears at; null when the map has none of the mode's.
    virtual SpawnPoint* ChooseSpawn(const PlayerController& player);
    // Makes the player's pawn from playerPrefab at a spawn point; null without a prefab.
    virtual Pawn* SpawnPawn(PlayerController& player);
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Player {
        Spot::EntityId controller = Spot::EntityId::Invalid;
        Spot::EntityId pawn = Spot::EntityId::Invalid;
        Spot::Vector3 start;
        Spot::InvokeId respawn = Spot::InvokeId::Invalid;
        Spot::SignalConnection died;
    };

    void TrackPlayers();
    void ScheduleRespawn(Spot::EntityId controller);
    void ReturnPlayer(Spot::EntityId controller, bool restoreHealth);

    std::vector<Player> m_players;
};

} // namespace Gameplay
