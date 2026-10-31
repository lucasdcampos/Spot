#pragma once

#include <spot.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Gameplay {

class Pawn;
class RemoteController;

// The players of a session in its scene. On the server, each client running the scene gets a RemoteController, which
// the game mode gives a pawn as it does local players, and owns the network object of the pawn it possesses; a client
// that leaves takes its controller and character with it, returning borrowed pawns to the server. On a client, the
// local player possesses the pawn it owns that the server has it possess, as its character or the vehicle it drives,
// and sends the server its intents every frame, with the last few before them in case those were lost. Discrete pawn
// requests are reliable and accepted only from the player currently possessing that pawn. Alone, it does nothing.
//
// A pawn its player's machine decides, as the player's character, moves there and is shown elsewhere as that machine
// says. The server checks it: no faster over a second than the pawn said it could go along the way
// (Pawn::GetSpeedLimit()), with some slack, and not flying through everything (the pawn state "Noclip"), a cheat,
// unless the player may cheat (Session::MayCheat()); failing, it is put back where it was a second before
// (Pawn::Teleport()), which is logged.
class NetworkPlayers final : public Spot::Component {
public:
    // Frames of intents each message repeats, so one lost on the way costs nothing.
    int repeatedFrames = 3;
    // Meters a pawn its player's machine decides may go beyond its speed limit over a second, for what the limit
    // does not know, as being shown a little late.
    float movementSlack = 1.5f;
    // How close to a player's character, and to the vehicle the player drives, a body the server simulates is given
    // to that player's machine to simulate (BodyAuthority), and for how many seconds nothing of the player's is near it
    // before the server takes it back.
    float handOffRadius = 1.5f;
    float vehicleHandOffRadius = 4.0f;
    float handBackDelay = 2.0f;
    Spot::AssetRef<Spot::Prefab> playerInfoPrefab;

    void Serialize(Spot::Archive& archive) override;

    // On the server: how many times a player's pawn was put back for moving as it may not, shown with the statistics
    // overlay (`stats`).
    std::uint32_t GetCorrections() const { return m_corrections; }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;

private:
    // Where a pawn a player's machine decides was lately, to check how it moves.
    struct Track {
        struct Point {
            double time = 0.0;
            Spot::Vector3 position;
            // How fast it could go then (Pawn::GetSpeedLimit()).
            float limit = 0.0f;
        };
        std::uint32_t teleports = 0;
        std::deque<Point> path;
    };

    void Bind(Spot::NetworkSystem& system);
    void UpdateServer(Spot::NetworkSystem& system);
    void UpdateClient();
    void UpdatePlayerInfos(Spot::NetworkSystem& system);
    // On the server: gives players' machines the bodies near what they simulate, and takes back those left alone.
    void HandOffBodies(const std::vector<RemoteController*>& controllers);
    // On the server: puts back the pawns players' machines move as they may not.
    void CheckMovement(const std::vector<RemoteController*>& controllers);
    void SendIntents(Spot::NetworkSystem& system);

    Spot::NetworkSystem* m_system = nullptr;
    std::uint32_t m_frame = 0;
    Spot::EntityId m_sentPawn = Spot::EntityId::Invalid;
    std::deque<std::string> m_sent;
    std::unordered_map<Spot::EntityId, Track> m_tracks;
    std::uint32_t m_corrections = 0;
    std::unique_ptr<Spot::StatsSection> m_stats;
    double m_time = 0.0;
    // Bodies given to players' machines, and when something of that player's was last near each.
    std::unordered_map<Spot::EntityId, double> m_lastNear;
};

} // namespace Gameplay
