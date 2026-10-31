#pragma once

#include "spot/core/ids/asset_id.h"
#include "spot/core/ids/asset_ref.h"
#include "spot/core/ids/entity_id.h"
#include "spot/net/net_archive.h"
#include "spot/net/net_messages.h"
#include "spot/scene/net/network_object.h"
#include "spot/scene/net/network_session.h"
#include "spot/scene/scene_system.h"
#include "spot/scene/signal.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Spot {

class Entity;
class Prefab;
class Scene;

struct NetworkSystemConfig {
    // Snapshots a second the server sends each client, and states a second a client sends of what it owns.
    float sendRate = 30.0f;
    // Bytes of object states a snapshot holds at most; the changes that do not fit wait for the next one, by priority.
    std::size_t snapshotBudget = 4000;
    // Seconds in the past clients show what others decide, at least, so there are states to move between when one is
    // late. Clients show it farther back while snapshots come less often or arrive less evenly, as when the server's
    // frames are slow, so what is shown always has a state after it, up to maxInterpolationDelay; with that at or
    // below interpolationDelay, the delay is fixed.
    float interpolationDelay = 0.1f;
    float maxInterpolationDelay = 0.5f;
    // How clients get the prefabs of what the server spawns; by default, from the running application's assets.
    std::function<AssetRef<Prefab>(AssetId)> loadPrefab;
};

// Keeps a scene in sync between the machines of a session. On the server, it gives every NetworkObject an id, and
// sends each client snapshots of what changed, until the client acknowledges them; on clients, it makes what the server
// spawned from the same prefabs, matches the objects of the scene's file by the ids they were saved with
// (Entity::GetSavedId()), and shows what others decide between
// the states it received. Objects a client owns with NetworkAuthority::Owner go the other way.
//
// A client takes part once its scene is bound and says which scene asset it runs (Scene::GetSourceAsset()): the server
// replicates to it only when it is the server's. NetworkModule binds the application's scenes; tests and games with
// their own loop add it to a scene with the session.
class NetworkSystem final : public SceneSystem, public NetEntityMapper {
public:
    // The system keeps the session alive while it is bound to it.
    explicit NetworkSystem(std::shared_ptr<NetworkSession> session, NetworkSystemConfig config = {});
    ~NetworkSystem() override;

    // Null once the session is over: the scene then plays alone, as if it had no system.
    NetworkSession* GetSession() const { return m_session.get(); }
    // Binds the scene to another session, starting over, or to none with null.
    void SetSession(std::shared_ptr<NetworkSession> session);
    const NetworkSystemConfig& GetConfig() const { return m_config; }
    bool IsServer() const { return !m_session || m_session->IsServer(); }
    bool IsClient() const { return m_session && !m_session->IsServer(); }

    // On the server: the clients running this scene, which get its objects.
    bool IsPeerReady(NetPeerId peer) const;
    std::vector<NetPeerId> GetReadyPeers() const;
    // A client started running this scene.
    Signal<NetPeerId> OnPeerReady;
    // On clients: whether the server took this scene as its own and sent its first snapshot.
    bool IsSynchronized() const { return m_synchronized; }

    // On the server: gives an object, and those below it, to a client, or back to the server with Invalid.
    void SetOwner(NetworkObject& object, NetPeerId owner);
    // On the server: what a client's player is, whose distance to objects makes them matter more or less to it.
    void SetFocus(NetPeerId peer, EntityId entity);

    NetworkObject* FindObject(NetworkId id) const;
    Entity* FindEntity(NetworkId id) const;

    // Messages about an object, to the machines that know it. The handler of its type gets the object and the sender.
    template <NetMessage T>
    void SendToServer(NetworkObject& object, T message, NetDelivery delivery = NetDelivery::Reliable) {
        if (IsClient()) {
            SendObjectMessage(m_session->GetServerPeer(), object, Encode(message), delivery);
        }
    }
    template <NetMessage T>
    void SendToOwner(NetworkObject& object, T message, NetDelivery delivery = NetDelivery::Reliable) {
        if (IsServer() && object.GetOwner() != NetPeerId::Invalid) {
            SendObjectMessage(object.GetOwner(), object, Encode(message), delivery);
        }
    }
    // From the server, to every client that runs the scene.
    template <NetMessage T>
    void SendToClients(NetworkObject& object, T message, NetDelivery delivery = NetDelivery::Reliable) {
        if (IsServer()) {
            const std::vector<std::byte> data = Encode(message);
            for (const NetPeerId peer : GetReadyPeers()) {
                SendObjectMessage(peer, object, data, delivery);
            }
        }
    }
    template <NetMessage T>
    void OnObjectMessage(std::function<void(NetworkObject& object, NetPeerId from, T& message)> handler) {
        m_objectMessages.On<T>([this, handler = std::move(handler)](NetPeerId from, T& message) {
            if (m_dispatching) {
                handler(*m_dispatching, from, message);
            }
        });
    }

    // On clients: the server's time that what others decide is shown at.
    double GetShownTime() const;
    // On clients: how far behind the server's time that is now, in seconds.
    double GetShownDelay() const;

    // Entity references in messages and replicated state: the network ids of replicated entities.
    std::uint64_t ToWire(EntityId entity) const override;
    EntityId FromWire(std::uint64_t id) const override;

    struct Statistics {
        std::size_t objects = 0;
        std::size_t snapshotsSent = 0;
        std::size_t snapshotsReceived = 0;
        std::size_t lastSnapshotBytes = 0;
        // This frame: objects shown from what others decide, and how many of them were moving and are past their
        // last state, waiting for the next (NetworkTransform::maxExtrapolation).
        std::size_t shownObjects = 0;
        std::size_t lateObjects = 0;
    };
    const Statistics& GetStatistics() const { return m_statistics; }

protected:
    void OnComponentAdded(Component& component) override;
    void OnComponentRemoved(Component& component) override;
    void OnEntityDestroyed(Entity& entity) override;
    void OnBeforeFixedSteps(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;

private:
    friend class NetworkSession;
    struct Object;
    struct Peer;
    struct State;

    template <NetMessage T>
    std::vector<std::byte> Encode(T& message) const {
        BitWriter writer;
        WriteNetMessage(writer, message, this);
        return writer.TakeBytes();
    }
    void SendObjectMessage(NetPeerId peer, NetworkObject& object, std::span<const std::byte> message,
                           NetDelivery delivery);

    void Bind();
    void TellScene();
    void Reset();
    void Receive(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data);
    void PeerDisconnected(NetPeerId peer);

    void ServerRegisterAdded();
    void ServerSend(float deltaTime);
    void ServerReceiveAck(NetPeerId peer, BitReader& reader);
    void ServerReceiveOwnerState(NetPeerId peer, BitReader& reader);
    void ClientReceiveSceneObjects(BitReader& reader);
    void ClientReceiveSnapshot(BitReader& reader);
    void ClientResolveReferences();
    void ClientSend(float deltaTime);

    // Its transform and its Replicated parts; from its owner, only those the owner decides.
    std::vector<std::byte> WriteState(NetworkObject& object, bool fromOwner = false) const;
    void ReadState(NetworkObject& object, std::span<const std::byte> state, double time, bool fromOwner = false);
    void ApplyAuthority(NetworkObject& object);
    void Forget(NetworkObject& object);

    std::shared_ptr<NetworkSession> m_session;
    NetworkSystemConfig m_config;
    std::unique_ptr<State> m_state;
    NetMessageHandlers m_objectMessages;
    NetworkObject* m_dispatching = nullptr;
    bool m_bound = false;
    bool m_synchronized = false;
    Statistics m_statistics;
};

// Who decides what, for gameplay code that runs alike with and without a session.
namespace Network {

// The scene's system, or null when it plays alone.
NetworkSystem* Get(const Scene& scene);
// Whether the scene runs the game's rules: alone or on the server.
bool IsServer(const Scene& scene);
// Whether the scene is a client's, showing what a server decides.
bool IsClient(const Scene& scene);
// Whether this machine decides the entity's state: alone or on the server, unless a client owns its object with
// NetworkAuthority::Owner; on a client, for objects it owns so, and for entities of no object, which only it has.
bool HasAuthority(const Entity& entity);
// The NetworkObject the entity is part of: its own, or its nearest ancestor's.
NetworkObject* GetObject(const Entity& entity);

} // namespace Network

} // namespace Spot
