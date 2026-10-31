#pragma once

#include <spot.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
// A predicted pawn (PredictedPart), as the player's character, moves on the player's machine at once: the intents of
// its predicted parts go with each fixed step instead, which the server applies as they were taken, telling the state
// they left. Where that differs from what the machine had after the same step, the machine starts again from it,
// stepping again what the server had not seen yet.
class NetworkPlayers final : public Spot::Component {
public:
    // Frames and steps of intents each message repeats, so one lost on the way costs nothing.
    int repeatedFrames = 3;
    // Meters a predicted pawn may be from where the server had it before it is corrected.
    float correctionTolerance = 0.02f;
    // How close to a player's character, and to the vehicle the player drives, a body the server simulates is given
    // to that player's machine to simulate (BodyAuthority), and for how many seconds nothing of the player's is near it
    // before the server takes it back.
    float handOffRadius = 1.5f;
    float vehicleHandOffRadius = 4.0f;
    float handBackDelay = 2.0f;
    Spot::AssetRef<Spot::Prefab> playerInfoPrefab;

    void Serialize(Spot::Archive& archive) override;

    // How many times the server set the local player's predicted pawn right, shown with the statistics overlay
    // (`stats`) as how many a second and how far the largest moved it.
    std::uint32_t GetCorrections() const { return m_corrections; }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;

private:
    friend class PredictionSystem;

    struct Step {
        std::uint32_t number = 0;
        std::string intents;
        // Where the pawn was after it.
        Spot::Vector3 position;
    };
    struct StepState {
        Spot::NetworkId object = Spot::NetworkId::Invalid;
        std::uint32_t step = 0;
        Spot::Vector3 position;
        std::string state;
    };

    // On clients, after each fixed step: what the predicted pawn's step was.
    void RecordStep();
    // On clients, before the fixed steps: starts again from what the server last said, if it differs.
    void Correct();
    Pawn* GetPredictedPawn() const;

    void Bind(Spot::NetworkSystem& system);
    void UpdateServer(Spot::NetworkSystem& system);
    void UpdateClient();
    void UpdatePlayerInfos(Spot::NetworkSystem& system);
    // On the server: gives players' machines the bodies near what they simulate, and takes back those left alone.
    void HandOffBodies(const std::vector<RemoteController*>& controllers);
    void SendIntents(Spot::NetworkSystem& system);
    void SendSteps(Spot::NetworkSystem& system, Spot::NetworkObject& object);

    Spot::NetworkSystem* m_system = nullptr;
    std::uint32_t m_frame = 0;
    Spot::EntityId m_sentPawn = Spot::EntityId::Invalid;
    std::deque<std::string> m_sent;
    // The predicted pawn's steps the server has not been seen to apply yet, oldest first.
    std::deque<Step> m_steps;
    Spot::EntityId m_predicted = Spot::EntityId::Invalid;
    std::uint32_t m_step = 0;
    std::uint32_t m_sentStep = 0;
    std::optional<StepState> m_stepState;
    std::uint32_t m_corrections = 0;
    // The corrections of the second being measured, and those of the last one.
    double m_measureStart = 0.0;
    std::uint32_t m_measureCorrections = 0;
    float m_measureLargest = 0.0f;
    float m_correctionRate = 0.0f;
    float m_largestCorrection = 0.0f;
    std::unique_ptr<Spot::StatsSection> m_stats;
    double m_time = 0.0;
    // Bodies given to players' machines, and when something of that player's was last near each.
    std::unordered_map<Spot::EntityId, double> m_lastNear;
};

} // namespace Gameplay
