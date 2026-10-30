#include "gameplay/network/network_players.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/players.h"
#include "gameplay/network/pawn_intents.h"
#include "gameplay/network/remote_controller.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace Gameplay {

SPOT_COMPONENT(NetworkPlayers);

namespace {

// A player's latest frames of intents, oldest first, the newest being frame.
struct IntentsMessage {
    static constexpr std::string_view NetName = "Gameplay.Intents";
    std::uint32_t frame = 0;
    std::vector<std::string> frames;

    void Serialize(Spot::Archive& archive) {
        archive.Field("frame", frame);
        archive.Field("frames", frames);
    }
};

RemoteController* FindController(Spot::Scene& scene, Spot::NetPeerId peer) {
    RemoteController* found = nullptr;
    scene.ForEachComponent<RemoteController>([&](RemoteController& controller) {
        if (controller.GetPeer() == peer) {
            found = &controller;
        }
    });
    return found;
}

} // namespace

void NetworkPlayers::Bind(Spot::NetworkSystem& system) {
    if (m_system == &system) {
        return;
    }
    m_system = &system;
    m_frame = 0;
    m_sent.clear();
    if (system.IsServer()) {
        system.OnObjectMessage<IntentsMessage>(
            [this](Spot::NetworkObject& object, Spot::NetPeerId from, IntentsMessage& message) {
                RemoteController* controller = FindController(GetScene(), from);
                Pawn* pawn = controller ? controller->GetPawn() : nullptr;
                // Only for the pawn that player possesses.
                if (!pawn || Spot::Network::GetObject(pawn->GetEntity()) != &object) {
                    return;
                }
                const auto count = static_cast<std::uint32_t>(message.frames.size());
                if (count > message.frame) {
                    return;
                }
                for (std::uint32_t i = 0; i < count; ++i) {
                    controller->Receive(message.frame - (count - 1 - i), std::move(message.frames[i]));
                }
            });
    }
}

void NetworkPlayers::OnUpdate(float /*deltaTime*/) {
    Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
    if (!system) {
        m_system = nullptr;
        return;
    }
    Bind(*system);
    if (system->IsServer()) {
        UpdateServer(*system);
    } else {
        UpdateClient();
    }
}

void NetworkPlayers::UpdateServer(Spot::NetworkSystem& system) {
    const std::vector<Spot::NetPeerId> ready = system.GetReadyPeers();
    for (const Spot::NetPeerId peer : ready) {
        if (!FindController(GetScene(), peer)) {
            Spot::Entity& entity = GetScene().CreateEntity("Remote Player " + std::to_string(static_cast<int>(peer)));
            entity.AddComponent<RemoteController>().SetPeer(peer);
        }
    }
    std::vector<RemoteController*> controllers;
    GetScene().ForEachComponent<RemoteController>(
        [&](RemoteController& controller) { controllers.push_back(&controller); });
    for (RemoteController* controller : controllers) {
        Pawn* pawn = controller->GetPawn();
        if (std::ranges::find(ready, controller->GetPeer()) == ready.end()) {
            // Its player left, or runs another scene: the pawn goes with the controller.
            if (pawn) {
                pawn->GetEntity().Destroy();
            }
            controller->GetEntity().Destroy();
            continue;
        }
        if (!pawn) {
            continue;
        }
        if (Spot::NetworkObject* object = Spot::Network::GetObject(pawn->GetEntity());
            object && object->GetOwner() != controller->GetPeer()) {
            system.SetOwner(*object, controller->GetPeer());
        }
        system.SetFocus(controller->GetPeer(), pawn->GetEntity().GetId());
    }
}

void NetworkPlayers::UpdateClient() {
    PlayerController* player = Players::Get(GetScene());
    if (!player) {
        return;
    }
    Pawn* owned = nullptr;
    GetScene().ForEachComponent<Pawn>([&](Pawn& pawn) {
        const Spot::NetworkObject* object = pawn.GetEntity().GetComponent<Spot::NetworkObject>();
        if (!owned && object && object->IsOwnedLocally()) {
            owned = &pawn;
        }
    });
    if (owned && player->GetPawn() != owned) {
        player->Possess(*owned);
    }
}

void NetworkPlayers::OnLateUpdate(float /*deltaTime*/) {
    if (m_system && Spot::Network::Get(GetScene()) == m_system && m_system->IsClient()) {
        SendIntents(*m_system);
    }
}

void NetworkPlayers::SendIntents(Spot::NetworkSystem& system) {
    const PlayerController* player = Players::Get(GetScene());
    Pawn* pawn = player ? player->GetPawn() : nullptr;
    Spot::NetworkObject* object = pawn ? Spot::Network::GetObject(pawn->GetEntity()) : nullptr;
    if (!object || !object->IsOwnedLocally()) {
        return;
    }
    Spot::BitWriter writer;
    PawnIntents::Write(*pawn, writer);
    const std::vector<std::byte> bytes = writer.TakeBytes();
    m_sent.emplace_back(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    while (m_sent.size() > static_cast<std::size_t>(std::max(repeatedFrames, 0)) + 1) {
        m_sent.pop_front();
    }
    IntentsMessage message{.frame = ++m_frame, .frames = {m_sent.begin(), m_sent.end()}};
    system.SendToServer(*object, std::move(message), Spot::NetDelivery::Unreliable);
}

void NetworkPlayers::Serialize(Spot::Archive& archive) {
    archive.Field("repeatedFrames", repeatedFrames,
                  {.min = 0.0f, .max = 10.0f, .tooltip = "Frames of intents each message repeats, against loss"});
}

} // namespace Gameplay
