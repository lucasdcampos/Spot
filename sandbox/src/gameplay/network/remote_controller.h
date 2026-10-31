#pragma once

#include "gameplay/core/controller.h"

#include <spot.h>

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace Gameplay {

// On a server, the controller of a player who plays on another machine: it applies to the pawn it possesses the
// intents that machine sends, a frame at a time and in order, before the scene's fixed steps. A predicted pawn
// (PredictedPart) it steps as that machine did, one step for each of its steps, no faster than time passes here
// unless they pile up, and tells the machine the state each batch of steps left.
class RemoteController final : public Controller {
public:
    Spot::NetPeerId GetPeer() const { return m_peer; }
    void SetPeer(Spot::NetPeerId peer) { m_peer = peer; }
    bool IsPlayer() const override { return true; }
    // The player's own pawn, kept while it temporarily possesses another, as when driving a vehicle.
    Pawn* GetPlayerPawn() const;

    // A frame's intents, as PawnIntents wrote them; frames already received, or older, are ignored.
    void Receive(std::uint32_t frame, std::string intents);
    // A fixed step's intents of a predicted pawn; steps already received, or older, are ignored.
    void ReceiveStep(std::uint32_t step, std::string intents);
    // The player's last step applied.
    std::uint32_t GetAppliedStep() const { return m_appliedStep; }

protected:
    void OnStart() override;
    void OnPossess(Pawn& target) override;
    void OnUnpossess(Pawn& target) override;

private:
    friend class RemoteIntentSystem;

    void ApplyReceived(float deltaTime);
    void ApplySteps(Pawn& target, float deltaTime);

    Spot::NetPeerId m_peer = Spot::NetPeerId::Invalid;
    Spot::EntityId m_playerPawn = Spot::EntityId::Invalid;
    std::uint32_t m_lastFrame = 0;
    std::vector<std::string> m_received;
    std::uint32_t m_lastStep = 0;
    std::uint32_t m_appliedStep = 0;
    std::deque<std::pair<std::uint32_t, std::string>> m_steps;
    // Seconds of steps it may still apply: time passing adds to it, each step takes from it.
    float m_stepTime = 0.0f;
};

} // namespace Gameplay
