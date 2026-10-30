#pragma once

#include "gameplay/core/controller.h"

#include <spot.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Gameplay {

// On a server, the controller of a player who plays on another machine: it applies to the pawn it possesses the
// intents that machine sends, a frame at a time and in order, before the scene's fixed steps.
class RemoteController final : public Controller {
public:
    Spot::NetPeerId GetPeer() const { return m_peer; }
    void SetPeer(Spot::NetPeerId peer) { m_peer = peer; }
    bool IsPlayer() const override { return true; }

    // A frame's intents, as PawnIntents wrote them; frames already received, or older, are ignored.
    void Receive(std::uint32_t frame, std::string intents);

protected:
    void OnStart() override;
    void OnUnpossess(Pawn& target) override;

private:
    friend class RemoteIntentSystem;

    void ApplyReceived();

    Spot::NetPeerId m_peer = Spot::NetPeerId::Invalid;
    std::uint32_t m_lastFrame = 0;
    std::vector<std::string> m_received;
};

} // namespace Gameplay
