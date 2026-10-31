#include "gameplay/network/network_players.h"

#include "gameplay/core/body_authority.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/pawn_request.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/players.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/network/pawn_intents.h"
#include "gameplay/network/player_info.h"
#include "gameplay/network/player_messages.h"
#include "gameplay/network/remote_controller.h"
#include "gameplay/network/session.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

SPOT_COMPONENT(NetworkPlayers);

namespace {

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

void NetworkPlayers::OnStart() {
    m_stats = std::make_unique<Spot::StatsSection>([this]() -> std::string {
        if (!m_system || !m_system->IsServer() || Spot::Network::Get(GetScene()) != m_system) {
            return {};
        }
        return std::format("Players put back: {}", m_corrections);
    });
}

void NetworkPlayers::Bind(Spot::NetworkSystem& system) {
    if (m_system == &system) {
        return;
    }
    m_system = &system;
    m_frame = 0;
    m_sentPawn = Spot::EntityId::Invalid;
    m_sent.clear();
    m_tracks.clear();
    if (system.IsClient()) {
        // The server teleported a pawn this machine decides.
        system.OnObjectMessage<PawnPlaced>([](Spot::NetworkObject& object, Spot::NetPeerId, PawnPlaced& placed) {
            if (auto* pawn = object.GetEntity().GetComponent<Pawn>(); pawn && object.HasAuthority()) {
                pawn->ResetMotion();
                pawn->Place(placed.position, placed.forward, placed.velocity);
            }
        });
    }
    if (system.IsServer()) {
        system.OnObjectMessage<PawnRequest>(
            [](Spot::NetworkObject& object, Spot::NetPeerId from, PawnRequest& request) {
                RemoteController* controller = FindController(object.GetScene(), from);
                Pawn* pawn = controller ? controller->GetPawn() : nullptr;
                if (pawn && Spot::Network::GetObject(pawn->GetEntity()) == &object) {
                    pawn->Request(request.name);
                }
            });
        system.OnObjectMessage<IntentsMessage>(
            [](Spot::NetworkObject& object, Spot::NetPeerId from, IntentsMessage& message) {
                RemoteController* controller = FindController(object.GetScene(), from);
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

void NetworkPlayers::OnUpdate(float deltaTime) {
    m_time += deltaTime;
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
            // The player's character leaves with it; a borrowed pawn, such as a vehicle, stays in the world.
            Pawn* character = controller->GetPlayerPawn();
            controller->Unpossess();
            if (character) {
                character->GetEntity().Destroy();
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
    std::erase_if(controllers,
                  [](const RemoteController* controller) { return controller->GetEntity().IsDestroyed(); });
    HandOffBodies(controllers);
    CheckMovement(controllers);
    UpdatePlayerInfos(system);
}

void NetworkPlayers::CheckMovement(const std::vector<RemoteController*>& controllers) {
    constexpr double Window = 1.0;
    const GameMode* mode = GameMode::Get(GetScene());
    std::unordered_map<Spot::EntityId, Track> tracks;
    for (RemoteController* controller : controllers) {
        Pawn* pawn = controller->GetPawn();
        const Spot::NetworkObject* object = pawn ? Spot::Network::GetObject(pawn->GetEntity()) : nullptr;
        if (!object || !object->IsDecidedByClient() || object->GetOwner() != controller->GetPeer()) {
            continue;
        }
        const Spot::EntityId id = pawn->GetEntity().GetId();
        Track track = m_tracks.contains(id) ? std::move(m_tracks[id]) : Track{};
        if (track.teleports != pawn->GetTeleportCount()) {
            track = {.teleports = pawn->GetTeleportCount()};
        }
        const Spot::Vector3 position = pawn->GetEntity().GetTransform().GetWorldPosition();
        track.path.emplace_back(m_time, position);
        while (track.path.size() > 2 && track.path[1].first <= m_time - Window) {
            track.path.pop_front();
        }
        const auto& [since, from] = track.path.front();
        // Falling is free; rising and going across take speed.
        const Spot::Vector3 moved = position - from;
        const float distance = Spot::Length(Spot::Vector3{moved.x, std::max(moved.y, 0.0f), moved.z});
        const float allowed = pawn->GetSpeedLimit() * static_cast<float>(m_time - since) * 1.25f + movementSlack;
        const bool flying = mode && !mode->allowsNoclip && pawn->HasState("Noclip");
        if (distance > allowed || flying) {
            Spot::Log::Warning("{}'s pawn moved {:.1f} m in {:.2f} s{}: put back", pawn->GetEntity().GetName(),
                               distance, m_time - since, flying ? " flying, which the mode forbids" : "");
            ++m_corrections;
            pawn->Teleport(from, {}, {});
            track = {.teleports = pawn->GetTeleportCount()};
        }
        tracks[id] = std::move(track);
    }
    m_tracks = std::move(tracks);
}

void NetworkPlayers::HandOffBodies(const std::vector<RemoteController*>& controllers) {
    auto& physics = GetScene().GetOrAddSystem<Spot::PhysicsWorld>();
    for (RemoteController* controller : controllers) {
        const Spot::NetPeerId peer = controller->GetPeer();
        // What the player's machine simulates: its character, and the vehicle it drives.
        std::vector<std::pair<Spot::Vector3, float>> around;
        Pawn* character = controller->GetPlayerPawn();
        if (character) {
            around.emplace_back(character->GetEntity().GetTransform().GetWorldPosition(), handOffRadius);
        }
        if (Pawn* pawn = controller->GetPawn(); pawn && pawn != character) {
            const Spot::NetworkObject* object = Spot::Network::GetObject(pawn->GetEntity());
            if (object && object->authority == Spot::NetworkAuthority::Owner && object->GetOwner() == peer) {
                around.emplace_back(pawn->GetEntity().GetTransform().GetWorldPosition(), vehicleHandOffRadius);
            }
        }
        for (const auto& [center, radius] : around) {
            for (const Spot::OverlapHit& hit : physics.OverlapSphere(center, radius)) {
                Spot::Entity* entity = GetScene().GetEntity(hit.entity);
                const Spot::NetworkObject* object = entity ? Spot::Network::GetObject(*entity) : nullptr;
                const auto* body = object ? object->GetEntity().GetComponent<Spot::Rigidbody>() : nullptr;
                if (!body) {
                    continue;
                }
                // Only what the server simulates here, or what the player already has.
                Spot::Entity& owner = object->GetEntity();
                if (BodyAuthority::GetHolder(owner) == peer ||
                    (!body->IsKinematic() && BodyAuthority::Give(owner, peer))) {
                    m_lastNear[owner.GetId()] = m_time;
                }
            }
        }
        // Given otherwise, as what the player carries, it is let go of the same way.
        for (const Spot::EntityId id : BodyAuthority::GetGiven(GetScene(), peer)) {
            m_lastNear.try_emplace(id, m_time);
        }
    }
    std::erase_if(m_lastNear, [this](const auto& entry) {
        Spot::Entity* entity = GetScene().GetEntity(entry.first);
        if (!entity || BodyAuthority::GetHolder(*entity) == Spot::NetPeerId::Invalid) {
            return true;
        }
        if (m_time - entry.second < handBackDelay) {
            return false;
        }
        BodyAuthority::TakeBack(*entity);
        return true;
    });
}

void NetworkPlayers::UpdatePlayerInfos(Spot::NetworkSystem& system) {
    if (!playerInfoPrefab) {
        return;
    }
    std::vector<Spot::EntityId> controllers;
    GetScene().ForEachComponent<Controller>([&](Controller& controller) {
        if (controller.IsPlayer()) {
            controllers.push_back(controller.GetEntity().GetId());
        }
    });
    std::vector<PlayerInfo*> infos = PlayerInfos::GetAll(GetScene());
    for (PlayerInfo* info : infos) {
        if (std::ranges::find(controllers, info->GetController()) == controllers.end()) {
            info->GetEntity().Destroy();
        }
    }
    for (const auto id : controllers) {
        auto* controller = GetScene().GetEntity(id)->GetComponent<Controller>();
        const auto* remote = dynamic_cast<RemoteController*>(controller);
        const auto peer = remote ? remote->GetPeer() : Spot::NetPeerId::Invalid;
        auto found = std::ranges::find_if(infos, [id](const PlayerInfo* info) { return info->GetController() == id; });
        PlayerInfo* info = found != infos.end() ? *found : nullptr;
        if (!info) {
            Spot::Entity* root = GetScene().Instantiate(playerInfoPrefab);
            info = root ? root->GetComponent<PlayerInfo>() : nullptr;
            if (!info) {
                if (root) {
                    root->Destroy();
                }
                continue;
            }
            info->SetController(id);
            info->peer = static_cast<std::uint32_t>(peer);
            if (Session* session = Session::Get()) {
                info->name = session->GetPlayerName(peer);
            } else {
                info->name = remote ? "Player " + std::to_string(info->peer) : "Host";
            }
            if (auto* object = info->GetEntity().GetComponent<Spot::NetworkObject>()) {
                system.SetOwner(*object, peer);
            }
            infos.push_back(info);
        }
        Pawn* pawn = remote ? remote->GetPlayerPawn() : controller->GetPawn();
        if (pawn && pawn->GetOccupant()) {
            pawn = pawn->GetOccupant();
        }
        info->pawn = pawn ? pawn->GetEntity().GetId() : Spot::EntityId::Invalid;
        info->ping = remote ? static_cast<int>(system.GetSession()->GetHost().GetStats(peer).roundTrip * 1000.0f) : 0;
    }
}

void NetworkPlayers::UpdateClient() {
    PlayerController* player = Players::Get(GetScene());
    if (!player) {
        return;
    }
    Pawn* owned = nullptr;
    GetScene().ForEachComponent<Pawn>([&](Pawn& pawn) {
        // The one the server has this player possess: the character, or what it drives.
        const Spot::NetworkObject* object = pawn.GetEntity().GetComponent<Spot::NetworkObject>();
        if (!owned && object && object->IsOwnedLocally() && pawn.IsPossessedWhereDecided()) {
            owned = &pawn;
        }
    });
    if (owned && player->GetPawn() != owned) {
        player->Possess(*owned);
    } else if (!owned && player->GetPawn()) {
        player->Unpossess();
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
    const Spot::EntityId id = object && object->IsOwnedLocally() ? pawn->GetEntity().GetId() : Spot::EntityId::Invalid;
    if (m_sentPawn != id) {
        m_sentPawn = id;
        // Different pawns may have different listeners. Never replay the old pawn's fields into the new one.
        m_sent.clear();
    }
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
    archive.Field(
        "handOffRadius", handOffRadius,
        {.min = 0.0f, .max = 20.0f, .tooltip = "How close to a player's character a body is given to its machine"});
    archive.Field(
        "vehicleHandOffRadius", vehicleHandOffRadius,
        {.min = 0.0f, .max = 50.0f, .tooltip = "How close to a driven vehicle a body is given to its driver"});
    archive.Field("handBackDelay", handBackDelay,
                  {.min = 0.0f, .max = 30.0f, .tooltip = "Seconds left alone before the server takes a body back"});
    archive.Field(
        "movementSlack", movementSlack,
        {.min = 0.0f, .max = 20.0f, .tooltip = "Meters a player's pawn may go beyond its speed over a second"});
    archive.Field("playerInfoPrefab", playerInfoPrefab);
}

} // namespace Gameplay
