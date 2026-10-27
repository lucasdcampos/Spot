#pragma once

#include <spot.h>

#include <vector>

namespace Gameplay {

class Pawn;
class PlayerController;

// The scene's rules for its local players. Remembers their starting pawns, possesses them, and returns them to
// their starting positions after death or falling out of the world. Games override RespawnPlayer() for other rules.
class GameMode : public Spot::Component {
public:
    float respawnTime = 4.0f;
    float fallHeight = -20.0f;
    Spot::AssetId restartScene;

    static GameMode* Get(Spot::Scene& scene);
    float GetRespawnTimeLeft(const PlayerController& player) const;
    void Restart();

protected:
    virtual void RespawnPlayer(PlayerController& player, Pawn& pawn, Spot::Vector3 start);
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
