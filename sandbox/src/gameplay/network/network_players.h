#pragma once

#include <spot.h>

#include <cstdint>
#include <deque>
#include <string>

namespace Gameplay {

// The players of a session in its scene. On the server, each client running the scene gets a RemoteController, which
// the game mode gives a pawn as it does local players, and owns that pawn's network object; a client that leaves
// takes its controller and pawn with it. On a client, the local player possesses the pawn it owns, and sends the
// server its intents every frame, with the last few before them in case those were lost. Alone, it does nothing.
class NetworkPlayers final : public Spot::Component {
public:
    // Frames of intents each message repeats, so one lost on the way costs nothing.
    int repeatedFrames = 3;

    void Serialize(Spot::Archive& archive) override;

protected:
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;

private:
    void Bind(Spot::NetworkSystem& system);
    void UpdateServer(Spot::NetworkSystem& system);
    void UpdateClient();
    void SendIntents(Spot::NetworkSystem& system);

    Spot::NetworkSystem* m_system = nullptr;
    std::uint32_t m_frame = 0;
    std::deque<std::string> m_sent;
};

} // namespace Gameplay
