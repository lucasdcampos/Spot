#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class Pawn;
class Controller;
class SpawnPoint;

// The scene's rules for its players, whose controllers say they are (Controller::IsPlayer()): the local ones, and on a
// server those of other machines. It runs where the game's rules do, alone or on the server, and does nothing on
// clients. Gives a player without a pawn one made from playerPrefab at a spawn point,
// remembers their starting pawns, possesses them, and returns them after death or falling out of the world: to a
// spawn point, the one farthest from the other players, or to where they started when the map has none. Games
// override RespawnPlayer() and ChooseSpawn() for other rules.
class GameMode : public Spot::Component, public Spot::Replicated {
public:
    std::string name = "Game";
    std::string description;
    int order = 0;
    bool allowsTimeScale = true;
    float respawnTime = 4.0f;
    float fallHeight = -20.0f;
    // The pawn of a player who has none.
    Spot::AssetRef<Spot::Prefab> playerPrefab;
    // The spawn points it uses: those of this tag and those without one; all of them when empty.
    std::string spawnTag;

    static GameMode* Get(Spot::Scene& scene);
    // Seconds until the dead player comes back; on clients, as the server said, for the player's pawn.
    float GetRespawnTimeLeft(const Controller& player) const;
    void Restart();

    // How long each dead player has left, for the clients' death screens.
    void Replicate(Spot::Archive& archive) override;

protected:
    virtual void RespawnPlayer(Controller& player, Pawn& pawn, Spot::Vector3 start);
    // The spawn point the player appears at; null when the map has none of the mode's.
    virtual SpawnPoint* ChooseSpawn(const Controller& player);
    // Makes the player's pawn from playerPrefab at a spawn point; null without a prefab.
    virtual Pawn* SpawnPawn(Controller& player);
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

    // A dead player's pawn and the seconds it had left, as a server said.
    struct Waiting {
        Spot::EntityId pawn = Spot::EntityId::Invalid;
        float left = 0.0f;

        void Serialize(Spot::Archive& archive);
    };

    void TrackPlayers();
    void ScheduleRespawn(Spot::EntityId controller);
    void ReturnPlayer(Spot::EntityId controller, bool restoreHealth);

    std::vector<Player> m_players;
    std::vector<Waiting> m_waiting;
};

} // namespace Gameplay
