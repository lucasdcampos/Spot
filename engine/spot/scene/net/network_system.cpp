#include "spot/scene/net/network_system.h"

#include "spot/assets/assets.h"
#include "spot/core/debug/log.h"
#include "spot/scene/entity.h"
#include "spot/scene/net/network_transform.h"
#include "spot/scene/net/replicated.h"
#include "spot/scene/physics/rigidbody.h"
#include "spot/scene/prefab.h"
#include "spot/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <unordered_set>

namespace Spot {

namespace {

enum class ReliableKind : std::uint32_t { SceneReady = 1, ObjectMessage = 2, SceneObjects = 3 };
enum class UnreliableKind : std::uint32_t { Ack = 1, OwnerState = 2, ObjectMessage = 3 };

// Snapshots a server remembers per client, waiting for their acknowledgement.
constexpr std::size_t SnapshotHistory = 64;

// The NetworkObjects of the entity and everything below it, inactive ones too, depth first in sibling order: the
// order two machines that made the same tree agree on.
void CollectObjects(Entity& entity, std::vector<NetworkObject*>& objects) {
    for (NetworkObject* object : entity.GetComponents<NetworkObject>()) {
        objects.push_back(object);
    }
    for (Entity* child : entity.GetChildren()) {
        CollectObjects(*child, objects);
    }
}

// The components that make up the object's state: the Replicated ones on its entity and below it, down to the next
// NetworkObject.
void CollectReplicated(Entity& entity, bool root, std::vector<Replicated*>& replicated) {
    if (!root && entity.GetComponent<NetworkObject>()) {
        return;
    }
    entity.ForEachComponent<Component>([&](Component& component) {
        if (auto* part = dynamic_cast<Replicated*>(&component)) {
            replicated.push_back(part);
        }
    });
    for (Entity* child : entity.GetChildren()) {
        CollectReplicated(*child, false, replicated);
    }
}

void WriteBlob(BitWriter& writer, BitWriter& blob) {
    const std::vector<std::byte> bytes = blob.TakeBytes();
    writer.WriteVarUInt(bytes.size());
    writer.WriteBytes(bytes);
}

std::vector<std::byte> ReadBlob(BitReader& reader) {
    const std::uint64_t size = reader.ReadVarUInt();
    if (!reader.IsValid() || size > reader.GetBitsLeft() / 8) {
        reader.Fail();
        return {};
    }
    return reader.ReadBytes(static_cast<std::size_t>(size));
}

void WriteAssetId(BitWriter& writer, AssetId id) {
    writer.WriteUInt64(id.high);
    writer.WriteUInt64(id.low);
}

AssetId ReadAssetId(BitReader& reader) {
    const std::uint64_t high = reader.ReadUInt64();
    return {high, reader.ReadUInt64()};
}

} // namespace

struct NetworkSystem::Object {
    NetworkObject* object = nullptr;
    EntityId entity = EntityId::Invalid;
    // Objects of the scene's file: the id they were saved with.
    std::optional<std::uint64_t> sceneKey;
    // Roots spawned at run time: their prefab, and the objects below them in order.
    AssetId prefab;
    std::vector<NetworkId> children;
    NetworkId root = NetworkId::Invalid;
    // On the server: the state written for the snapshots being sent.
    std::vector<std::byte> state;
};

struct NetworkSystem::Peer {
    struct Known {
        std::uint32_t ackedSequence = 0;
        bool spawned = false;
        bool ackedOwned = false;
        bool sent = false;
        std::vector<std::byte> ackedState;
        float accumulator = 0.0f;
    };
    struct Despawn {
        bool byKey = false;
        std::uint64_t value = 0;

        friend bool operator==(const Despawn&, const Despawn&) = default;
    };
    struct Sent {
        NetworkId id = NetworkId::Invalid;
        bool owned = false;
        std::vector<std::byte> state;
    };
    struct Snapshot {
        std::uint32_t sequence = 0;
        bool acknowledged = false;
        std::vector<Sent> sent;
        std::vector<Despawn> despawns;
    };

    bool ready = false;
    EntityId focus = EntityId::Invalid;
    std::unordered_map<NetworkId, Known> known;
    std::vector<Despawn> pendingDespawns;
    std::deque<Snapshot> history;
    std::uint32_t nextSequence = 1;
};

struct NetworkSystem::State {
    std::unordered_map<NetworkId, Object> objects;
    std::unordered_map<const NetworkObject*, NetworkId> ids;
    std::uint32_t nextId = 1;
    // The objects of the scene's file, by the ids they were saved with.
    std::unordered_map<std::uint64_t, NetworkObject*> sceneObjects;
    // On the server: objects added since the last snapshot, to spawn once their trees are complete.
    std::vector<EntityId> added;
    std::unordered_map<NetPeerId, Peer> peers;
    // The bodies made kinematic as another machine decides them, with whether they were before.
    std::unordered_map<const NetworkObject*, bool> kinematic;
    float sendTimer = 0.0f;
    // On clients: whether the server was told which scene this one runs.
    bool toldScene = false;
    // On clients: the snapshots received, to acknowledge.
    std::uint32_t lastReceived = 0;
    std::uint32_t receivedMask = 0;
    bool ackDue = false;
};

NetworkSystem::NetworkSystem(std::shared_ptr<NetworkSession> session, NetworkSystemConfig config)
    : m_session(std::move(session)), m_config(std::move(config)), m_state(std::make_unique<State>()) {
}

NetworkSystem::~NetworkSystem() {
    if (m_session) {
        m_session->Unbind(this);
    }
}

void NetworkSystem::SetSession(std::shared_ptr<NetworkSession> session) {
    if (session == m_session) {
        return;
    }
    Reset();
    if (m_session) {
        m_session->Unbind(this);
    }
    m_session = std::move(session);
}

void NetworkSystem::Reset() {
    // Objects go back to how they were before any session: the server's, simulated here.
    for (auto& [id, entry] : m_state->objects) {
        if (entry.object) {
            entry.object->m_id = NetworkId::Invalid;
            entry.object->m_owner = NetPeerId::Invalid;
            entry.object->m_ownedLocally = false;
        }
    }
    for (const auto& [object, wasKinematic] : m_state->kinematic) {
        if (auto* body = object->GetEntity().GetComponent<Rigidbody>()) {
            body->SetKinematic(wasKinematic);
        }
    }
    m_state = std::make_unique<State>();
    m_bound = false;
    m_synchronized = false;
}

bool NetworkSystem::IsPeerReady(NetPeerId peer) const {
    const auto found = m_state->peers.find(peer);
    return found != m_state->peers.end() && found->second.ready;
}

std::vector<NetPeerId> NetworkSystem::GetReadyPeers() const {
    std::vector<NetPeerId> ready;
    if (!m_session) {
        return ready;
    }
    for (const NetPeerId peer : m_session->GetHost().GetPeers()) {
        if (IsPeerReady(peer)) {
            ready.push_back(peer);
        }
    }
    return ready;
}

void NetworkSystem::SetOwner(NetworkObject& object, NetPeerId owner) {
    if (!IsServer()) {
        return;
    }
    object.m_owner = owner;
    ApplyAuthority(object);
    const auto found = m_state->ids.find(&object);
    if (found == m_state->ids.end()) {
        return;
    }
    for (const NetworkId child : m_state->objects[found->second].children) {
        if (NetworkObject* nested = FindObject(child)) {
            nested->m_owner = owner;
            ApplyAuthority(*nested);
        }
    }
}

void NetworkSystem::SetFocus(NetPeerId peer, EntityId entity) {
    if (const auto found = m_state->peers.find(peer); found != m_state->peers.end()) {
        found->second.focus = entity;
    }
}

NetworkObject* NetworkSystem::FindObject(NetworkId id) const {
    const auto found = m_state->objects.find(id);
    return found != m_state->objects.end() ? found->second.object : nullptr;
}

Entity* NetworkSystem::FindEntity(NetworkId id) const {
    const auto found = m_state->objects.find(id);
    return found != m_state->objects.end() ? GetScene().GetEntity(found->second.entity) : nullptr;
}

double NetworkSystem::GetShownTime() const {
    if (!m_session) {
        return 0.0;
    }
    return m_session->GetTime() - static_cast<double>(std::max(m_config.interpolationDelay, 0.0f));
}

std::uint64_t NetworkSystem::ToWire(EntityId entity) const {
    const Entity* found = GetScene().GetEntity(entity);
    const NetworkObject* object = found ? found->GetComponent<NetworkObject>() : nullptr;
    return object ? static_cast<std::uint64_t>(object->GetNetworkId()) : 0;
}

EntityId NetworkSystem::FromWire(std::uint64_t id) const {
    const Entity* entity = id <= 0xFFFFFFFFu ? FindEntity(NetworkId{static_cast<std::uint32_t>(id)}) : nullptr;
    return entity ? entity->GetId() : EntityId::Invalid;
}

void NetworkSystem::SendObjectMessage(NetPeerId peer, NetworkObject& object, std::span<const std::byte> message,
                                      NetDelivery delivery) {
    if (!m_session || object.GetNetworkId() == NetworkId::Invalid || peer == NetPeerId::Invalid) {
        return;
    }
    const bool reliable = delivery == NetDelivery::Reliable;
    BitWriter writer;
    writer.WriteBits(static_cast<std::uint32_t>(reliable ? static_cast<std::uint32_t>(ReliableKind::ObjectMessage)
                                                         : static_cast<std::uint32_t>(UnreliableKind::ObjectMessage)),
                     8);
    writer.WriteVarUInt(static_cast<std::uint32_t>(object.GetNetworkId()));
    writer.WriteBytes(message);
    m_session->GetHost().Send(peer, reliable ? NetworkSession::ReplicationChannel : NetworkSession::UnreliableChannel,
                              writer.TakeBytes(), delivery);
}

void NetworkSystem::OnComponentAdded(Component& component) {
    if (m_bound && IsServer() && dynamic_cast<NetworkObject*>(&component)) {
        m_state->added.push_back(component.GetEntity().GetId());
    }
}

void NetworkSystem::OnComponentRemoved(Component& component) {
    if (auto* object = dynamic_cast<NetworkObject*>(&component)) {
        Forget(*object);
    }
}

void NetworkSystem::OnEntityDestroyed(Entity& entity) {
    for (NetworkObject* object : entity.GetComponents<NetworkObject>()) {
        Forget(*object);
    }
}

void NetworkSystem::Bind() {
    if (m_bound || !m_session) {
        return;
    }
    m_bound = true;
    m_session->Bind(this);

    // Objects of the scene's file are the same on every machine, known by the ids they were saved with; others
    // were made since, and are spawned.
    std::vector<NetworkObject*> objects;
    for (Entity* root : GetScene().GetRootEntities()) {
        CollectObjects(*root, objects);
    }
    for (NetworkObject* object : objects) {
        const std::uint64_t key = object->GetEntity().GetSavedId();
        if (key == 0) {
            if (IsServer()) {
                m_state->added.push_back(object->GetEntity().GetId());
            }
            continue;
        }
        m_state->sceneObjects[key] = object;
        if (IsServer()) {
            const NetworkId id{m_state->nextId++};
            object->m_id = id;
            m_state->ids[object] = id;
            m_state->objects[id] = {.object = object, .entity = object->GetEntity().GetId(), .sceneKey = key};
        }
        ApplyAuthority(*object);
    }
    m_statistics.objects = m_state->objects.size();
}

void NetworkSystem::TellScene() {
    // Once connected; again after connecting anew.
    const bool connected = m_session->IsConnected();
    if (!IsClient() || m_state->toldScene == connected) {
        return;
    }
    m_state->toldScene = connected;
    if (!connected) {
        return;
    }
    BitWriter writer;
    writer.WriteBits(static_cast<std::uint32_t>(ReliableKind::SceneReady), 8);
    WriteAssetId(writer, GetScene().GetSourceAsset());
    m_session->GetHost().Send(m_session->GetServerPeer(), NetworkSession::ReplicationChannel, writer.TakeBytes(),
                              NetDelivery::Reliable);
}

void NetworkSystem::OnBeforeFixedSteps(float /*deltaTime*/) {
    if (!m_session) {
        return;
    }
    Bind();
    // Keeps it alive should a handler end the session.
    const std::shared_ptr<NetworkSession> session = m_session;
    session->Update();
    if (m_session) {
        TellScene();
    }
}

void NetworkSystem::OnLateUpdate(float deltaTime) {
    if (!m_session) {
        return;
    }
    Bind();
    const std::shared_ptr<NetworkSession> session = m_session;
    session->Update();
    if (!m_session) {
        return;
    }
    TellScene();
    if (IsServer()) {
        ServerSend(deltaTime);
    } else {
        ClientSend(deltaTime);
    }

    const double shown = GetShownTime();
    for (auto& [id, entry] : m_state->objects) {
        if (entry.object && !entry.object->HasAuthority()) {
            if (auto* transform = entry.object->GetEntity().GetComponent<NetworkTransform>()) {
                transform->ShowAt(shown);
            }
        }
    }
    session->Update();
}

void NetworkSystem::ApplyAuthority(NetworkObject& object) {
    auto* body = object.GetEntity().GetComponent<Rigidbody>();
    if (!body) {
        return;
    }
    const bool authority = object.HasAuthority();
    const auto made = m_state->kinematic.find(&object);
    if (!authority && made == m_state->kinematic.end()) {
        m_state->kinematic[&object] = body->IsKinematic();
        body->SetKinematic(true);
    } else if (authority && made != m_state->kinematic.end()) {
        body->SetKinematic(made->second);
        m_state->kinematic.erase(made);
    }
    if (authority) {
        if (auto* transform = object.GetEntity().GetComponent<NetworkTransform>()) {
            transform->m_samples.clear();
        }
    }
}

void NetworkSystem::Forget(NetworkObject& object) {
    m_state->kinematic.erase(&object);
    if (const auto scene = m_state->sceneObjects.find(object.GetEntity().GetSavedId());
        scene != m_state->sceneObjects.end() && scene->second == &object) {
        m_state->sceneObjects.erase(scene);
    }
    const auto found = m_state->ids.find(&object);
    if (found == m_state->ids.end()) {
        return;
    }
    const NetworkId id = found->second;
    const std::optional<std::uint64_t> key = m_state->objects[id].sceneKey;
    m_state->ids.erase(found);
    m_state->objects.erase(id);
    m_statistics.objects = m_state->objects.size();
    if (!IsServer()) {
        return;
    }
    for (auto& [peerId, peer] : m_state->peers) {
        const auto known = peer.known.find(id);
        // Objects of the scene are destroyed by their place in it, which every client knows them by.
        if (key) {
            if (peer.ready) {
                peer.pendingDespawns.push_back({.byKey = true, .value = *key});
            }
        } else if (known != peer.known.end() && known->second.sent) {
            peer.pendingDespawns.push_back({.value = static_cast<std::uint32_t>(id)});
        }
        if (known != peer.known.end()) {
            peer.known.erase(known);
        }
    }
}

void NetworkSystem::PeerDisconnected(NetPeerId peer) {
    m_state->peers.erase(peer);
    if (!IsServer()) {
        return;
    }
    // What the client owned goes back to the server.
    for (auto& [id, entry] : m_state->objects) {
        if (entry.object && entry.object->GetOwner() == peer) {
            entry.object->m_owner = NetPeerId::Invalid;
            ApplyAuthority(*entry.object);
        }
    }
}

void NetworkSystem::Receive(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data) {
    BitReader reader(data);
    if (channel == NetworkSession::SnapshotChannel) {
        if (IsClient()) {
            ClientReceiveSnapshot(reader);
        }
        return;
    }
    const std::uint32_t kind = reader.ReadBits(8);
    if (!reader.IsValid()) {
        return;
    }
    const bool reliable = channel == NetworkSession::ReplicationChannel;
    if (reliable && kind == static_cast<std::uint32_t>(ReliableKind::SceneReady)) {
        const AssetId scene = ReadAssetId(reader);
        if (IsServer() && reader.IsValid() && scene == GetScene().GetSourceAsset()) {
            Peer& state = m_state->peers[peer] = Peer{};
            state.ready = true;
            // The scene's objects that still exist: the client lets go of the others, destroyed before it came.
            std::vector<std::uint64_t> keys;
            keys.reserve(m_state->sceneObjects.size());
            for (const auto& [key, object] : m_state->sceneObjects) {
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            BitWriter writer;
            writer.WriteBits(static_cast<std::uint32_t>(ReliableKind::SceneObjects), 8);
            writer.WriteVarUInt(keys.size());
            std::uint64_t previous = 0;
            for (const std::uint64_t key : keys) {
                writer.WriteVarUInt(key - previous);
                previous = key;
            }
            m_session->GetHost().Send(peer, NetworkSession::ReplicationChannel, writer.TakeBytes(),
                                      NetDelivery::Reliable);
            OnPeerReady.Emit(peer);
        }
        return;
    }
    if (reliable && kind == static_cast<std::uint32_t>(ReliableKind::SceneObjects)) {
        if (IsClient()) {
            ClientReceiveSceneObjects(reader);
        }
        return;
    }
    if ((reliable && kind == static_cast<std::uint32_t>(ReliableKind::ObjectMessage)) ||
        (!reliable && kind == static_cast<std::uint32_t>(UnreliableKind::ObjectMessage))) {
        const auto id = NetworkId{static_cast<std::uint32_t>(reader.ReadVarUInt())};
        NetworkObject* object = reader.IsValid() ? FindObject(id) : nullptr;
        if (!object || (IsServer() && !IsPeerReady(peer))) {
            return;
        }
        m_dispatching = object;
        m_objectMessages.Dispatch(peer, reader, this);
        m_dispatching = nullptr;
        return;
    }
    if (!reliable && kind == static_cast<std::uint32_t>(UnreliableKind::Ack) && IsServer()) {
        ServerReceiveAck(peer, reader);
    } else if (!reliable && kind == static_cast<std::uint32_t>(UnreliableKind::OwnerState) && IsServer()) {
        ServerReceiveOwnerState(peer, reader);
    }
}

std::vector<std::byte> NetworkSystem::WriteState(NetworkObject& object) const {
    BitWriter writer;
    Entity& entity = object.GetEntity();
    if (auto* transform = entity.GetComponent<NetworkTransform>()) {
        writer.WriteBool(true);
        BitWriter blob;
        transform->Write(blob);
        WriteBlob(writer, blob);
    } else {
        writer.WriteBool(false);
    }
    std::vector<Replicated*> replicated;
    CollectReplicated(entity, true, replicated);
    writer.WriteVarUInt(replicated.size());
    for (Replicated* part : replicated) {
        BitWriter blob;
        NetArchiveWriter backend(blob, this);
        Archive archive(backend);
        part->Replicate(archive);
        WriteBlob(writer, blob);
    }
    return writer.TakeBytes();
}

void NetworkSystem::ReadState(NetworkObject& object, std::span<const std::byte> state, double time) {
    BitReader reader(state);
    Entity& entity = object.GetEntity();
    if (reader.ReadBool()) {
        const std::vector<std::byte> blob = ReadBlob(reader);
        if (auto* transform = entity.GetComponent<NetworkTransform>(); transform && reader.IsValid()) {
            BitReader transformReader(blob);
            const bool first = transform->m_samples.empty();
            transform->Read(transformReader, time);
            // A new object starts where it is, rather than moving there from where it was made.
            if (first) {
                transform->ShowAt(time);
            }
        }
    }
    std::vector<Replicated*> replicated;
    CollectReplicated(entity, true, replicated);
    const std::uint64_t count = reader.ReadVarUInt();
    for (std::uint64_t i = 0; i < count && reader.IsValid(); ++i) {
        const std::vector<std::byte> blob = ReadBlob(reader);
        if (!reader.IsValid() || i >= replicated.size()) {
            continue;
        }
        BitReader partReader(blob);
        NetArchiveReader backend(partReader, this);
        Archive archive(backend);
        replicated[i]->Replicate(archive);
        if (partReader.IsValid()) {
            replicated[i]->OnReplicated();
        }
    }
}

void NetworkSystem::ServerRegisterAdded() {
    std::vector<EntityId> added = std::exchange(m_state->added, {});
    for (const EntityId id : added) {
        Entity* entity = GetScene().GetEntity(id);
        if (!entity) {
            continue;
        }
        for (NetworkObject* object : entity->GetComponents<NetworkObject>()) {
            if (m_state->ids.contains(object)) {
                continue;
            }
            // The topmost object above it is the one spawned; those below are its parts.
            Entity* rootEntity = entity;
            for (Entity* parent = entity->GetParent(); parent; parent = parent->GetParent()) {
                if (parent->GetComponent<NetworkObject>()) {
                    rootEntity = parent;
                }
            }
            NetworkObject* root = rootEntity->GetComponent<NetworkObject>();
            if (m_state->ids.contains(root)) {
                Log::Warning("'{}' got a NetworkObject below a replicated object; only whole prefabs are spawned, so "
                             "clients do not see it",
                             entity->GetName());
                continue;
            }
            if (!rootEntity->GetPrefabAssetId().IsValid()) {
                Log::Warning("'{}' was not made from a prefab, so clients cannot make it", rootEntity->GetName());
                continue;
            }
            std::vector<NetworkObject*> parts;
            CollectObjects(*rootEntity, parts);
            const NetworkId rootId{m_state->nextId++};
            Object rootEntry{.object = root, .entity = rootEntity->GetId(), .prefab = rootEntity->GetPrefabAssetId()};
            root->m_id = rootId;
            m_state->ids[root] = rootId;
            for (NetworkObject* part : parts) {
                if (part == root) {
                    continue;
                }
                const NetworkId partId{m_state->nextId++};
                part->m_id = partId;
                m_state->ids[part] = partId;
                m_state->objects[partId] = {.object = part, .entity = part->GetEntity().GetId(), .root = rootId};
                rootEntry.children.push_back(partId);
            }
            m_state->objects[rootId] = std::move(rootEntry);
        }
    }
    m_statistics.objects = m_state->objects.size();
}

void NetworkSystem::ServerSend(float deltaTime) {
    ServerRegisterAdded();
    m_state->sendTimer += deltaTime;
    const float interval = 1.0f / std::max(m_config.sendRate, 1.0f);
    if (m_state->sendTimer < interval) {
        return;
    }
    const float elapsed = m_state->sendTimer;
    m_state->sendTimer = std::fmod(m_state->sendTimer, interval);

    for (auto& [id, entry] : m_state->objects) {
        if (entry.object) {
            entry.state = WriteState(*entry.object);
        }
    }

    const double time = m_session->GetTime();
    for (auto& [peerId, peer] : m_state->peers) {
        if (!peer.ready || !m_session->GetHost().IsConnected(peerId)) {
            continue;
        }
        const Entity* focus = GetScene().GetEntity(peer.focus);
        struct Candidate {
            NetworkId id;
            float accumulator;
        };
        std::vector<Candidate> candidates;
        for (auto& [id, entry] : m_state->objects) {
            if (!entry.object) {
                continue;
            }
            Peer::Known& known = peer.known[id];
            const bool owned = entry.object->GetOwner() == peerId;
            if (known.spawned && known.ackedOwned == owned && known.ackedState == entry.state) {
                continue;
            }
            float scale = 1.0f;
            const float radius = entry.object->relevanceRadius;
            if (focus && radius > 0.0f) {
                const float distance = Distance(focus->GetTransform().GetWorldPosition(),
                                                entry.object->GetEntity().GetTransform().GetWorldPosition());
                scale = radius / std::max(radius, distance);
            }
            known.accumulator += std::max(entry.object->priority, 0.01f) * scale * elapsed;
            // What has never been seen comes first, so clients have it to apply its parts' states to.
            candidates.push_back({id, known.spawned ? known.accumulator : known.accumulator + 1000.0f});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.accumulator != b.accumulator ? a.accumulator > b.accumulator : a.id < b.id;
        });

        Peer::Snapshot snapshot{.sequence = peer.nextSequence++, .despawns = peer.pendingDespawns};
        BitWriter writer;
        writer.WriteBits(snapshot.sequence, 32);
        writer.WriteDouble(time);
        writer.WriteVarUInt(snapshot.despawns.size());
        for (const Peer::Despawn& despawn : snapshot.despawns) {
            writer.WriteBool(despawn.byKey);
            writer.WriteVarUInt(despawn.value);
        }

        BitWriter entries;
        std::size_t count = 0;
        std::unordered_set<NetworkId> written;
        for (const Candidate& candidate : candidates) {
            if (entries.GetBitCount() / 8 >= m_config.snapshotBudget) {
                break;
            }
            Object& entry = m_state->objects[candidate.id];
            Peer::Known& known = peer.known[candidate.id];
            // A part needs its root on the client first.
            if (entry.root != NetworkId::Invalid && !peer.known[entry.root].spawned && !written.contains(entry.root)) {
                continue;
            }
            const bool owned = entry.object->GetOwner() == peerId;
            entries.WriteVarUInt(static_cast<std::uint32_t>(candidate.id));
            entries.WriteBool(!known.spawned);
            if (!known.spawned) {
                entries.WriteBool(entry.sceneKey.has_value());
                if (entry.sceneKey) {
                    entries.WriteVarUInt(*entry.sceneKey);
                } else {
                    WriteAssetId(entries, entry.prefab);
                    entries.WriteVarUInt(entry.children.size());
                    for (const NetworkId child : entry.children) {
                        entries.WriteVarUInt(static_cast<std::uint32_t>(child));
                    }
                }
            }
            entries.WriteBool(owned);
            entries.WriteVarUInt(entry.state.size());
            entries.WriteBytes(entry.state);
            known.sent = true;
            known.accumulator = 0.0f;
            written.insert(candidate.id);
            snapshot.sent.push_back({.id = candidate.id, .owned = owned, .state = entry.state});
            ++count;
        }
        writer.WriteVarUInt(count);
        const std::vector<std::byte> body = entries.TakeBytes();
        writer.WriteBytes(body);
        const std::vector<std::byte> packet = writer.TakeBytes();
        m_session->GetHost().Send(peerId, NetworkSession::SnapshotChannel, packet, NetDelivery::Sequenced);
        m_statistics.lastSnapshotBytes = packet.size();
        ++m_statistics.snapshotsSent;

        peer.history.push_back(std::move(snapshot));
        if (peer.history.size() > SnapshotHistory) {
            peer.history.pop_front();
        }
    }
}

void NetworkSystem::ServerReceiveAck(NetPeerId peerId, BitReader& reader) {
    const std::uint32_t latest = reader.ReadBits(32);
    const std::uint32_t mask = reader.ReadBits(32);
    const auto found = m_state->peers.find(peerId);
    if (!reader.IsValid() || found == m_state->peers.end()) {
        return;
    }
    Peer& peer = found->second;
    const auto acknowledged = [&](std::uint32_t sequence) {
        if (sequence == latest) {
            return true;
        }
        const std::uint32_t behind = latest - sequence;
        return sequence < latest && behind <= 32 && (mask & (1u << (behind - 1))) != 0;
    };
    for (Peer::Snapshot& snapshot : peer.history) {
        if (snapshot.acknowledged || !acknowledged(snapshot.sequence)) {
            continue;
        }
        snapshot.acknowledged = true;
        for (Peer::Sent& sent : snapshot.sent) {
            const auto known = peer.known.find(sent.id);
            if (known == peer.known.end() || snapshot.sequence <= known->second.ackedSequence) {
                continue;
            }
            known->second.ackedSequence = snapshot.sequence;
            known->second.spawned = true;
            known->second.ackedOwned = sent.owned;
            known->second.ackedState = std::move(sent.state);
        }
        for (const Peer::Despawn& despawn : snapshot.despawns) {
            std::erase(peer.pendingDespawns, despawn);
        }
    }
}

void NetworkSystem::ServerReceiveOwnerState(NetPeerId peer, BitReader& reader) {
    const auto id = NetworkId{static_cast<std::uint32_t>(reader.ReadVarUInt())};
    const std::vector<std::byte> state = ReadBlob(reader);
    NetworkObject* object = reader.IsValid() ? FindObject(id) : nullptr;
    if (object && object->authority == NetworkAuthority::Owner && object->GetOwner() == peer) {
        ReadState(*object, state, m_session->GetTime());
    }
}

void NetworkSystem::ClientReceiveSceneObjects(BitReader& reader) {
    const std::uint64_t count = reader.ReadVarUInt();
    std::unordered_set<std::uint64_t> alive;
    std::uint64_t key = 0;
    for (std::uint64_t i = 0; i < count && reader.IsValid(); ++i) {
        key += reader.ReadVarUInt();
        alive.insert(key);
    }
    if (!reader.IsValid()) {
        return;
    }
    std::vector<NetworkObject*> gone;
    for (const auto& [sceneKey, object] : m_state->sceneObjects) {
        if (!alive.contains(sceneKey)) {
            gone.push_back(object);
        }
    }
    for (NetworkObject* object : gone) {
        Entity& entity = object->GetEntity();
        Forget(*object);
        entity.Destroy();
    }
}

void NetworkSystem::ClientReceiveSnapshot(BitReader& reader) {
    const std::uint32_t sequence = reader.ReadBits(32);
    const double time = reader.ReadDouble();
    if (!reader.IsValid()) {
        return;
    }
    if (sequence > m_state->lastReceived) {
        const std::uint32_t ahead = sequence - m_state->lastReceived;
        m_state->receivedMask = ahead >= 32 ? 0u : (m_state->receivedMask << ahead);
        if (m_state->lastReceived != 0 && ahead <= 32) {
            m_state->receivedMask |= 1u << (ahead - 1);
        }
        m_state->lastReceived = sequence;
    } else if (const std::uint32_t behind = m_state->lastReceived - sequence; behind > 0 && behind <= 32) {
        m_state->receivedMask |= 1u << (behind - 1);
    }
    m_state->ackDue = true;
    m_session->ObserveServerTime(time);
    m_synchronized = true;
    ++m_statistics.snapshotsReceived;

    const std::uint64_t despawns = reader.ReadVarUInt();
    for (std::uint64_t i = 0; i < despawns && reader.IsValid(); ++i) {
        const bool byKey = reader.ReadBool();
        const std::uint64_t value = reader.ReadVarUInt();
        NetworkObject* object = nullptr;
        if (byKey) {
            const auto found = m_state->sceneObjects.find(value);
            object = found != m_state->sceneObjects.end() ? found->second : nullptr;
        } else {
            object = FindObject(NetworkId{static_cast<std::uint32_t>(value)});
        }
        if (object && reader.IsValid()) {
            Entity& entity = object->GetEntity();
            Forget(*object);
            entity.Destroy();
        }
    }

    const std::uint64_t count = reader.ReadVarUInt();
    for (std::uint64_t i = 0; i < count && reader.IsValid(); ++i) {
        const auto id = NetworkId{static_cast<std::uint32_t>(reader.ReadVarUInt())};
        if (reader.ReadBool()) {
            const bool sceneObject = reader.ReadBool();
            if (sceneObject) {
                const std::uint64_t key = reader.ReadVarUInt();
                const auto found = m_state->sceneObjects.find(key);
                NetworkObject* object = found != m_state->sceneObjects.end() ? found->second : nullptr;
                if (reader.IsValid() && object && !m_state->ids.contains(object) && !FindObject(id)) {
                    object->m_id = id;
                    m_state->ids[object] = id;
                    m_state->objects[id] = {.object = object, .entity = object->GetEntity().GetId(), .sceneKey = key};
                }
            } else {
                const AssetId prefabId = ReadAssetId(reader);
                std::vector<NetworkId> children(
                    static_cast<std::size_t>(std::min<std::uint64_t>(reader.ReadVarUInt(), reader.GetBitsLeft())));
                for (NetworkId& child : children) {
                    child = NetworkId{static_cast<std::uint32_t>(reader.ReadVarUInt())};
                }
                if (reader.IsValid() && !FindObject(id)) {
                    AssetRef<Prefab> prefab = m_config.loadPrefab ? m_config.loadPrefab(prefabId) : AssetRef<Prefab>();
                    if (!prefab && Assets::GetManager()) {
                        prefab = Assets::GetManager()->Load<Prefab>(prefabId);
                    }
                    Entity* root = prefab ? GetScene().Instantiate(prefab) : nullptr;
                    NetworkObject* rootObject = root ? root->GetComponent<NetworkObject>() : nullptr;
                    if (!rootObject) {
                        Log::Warning("Could not make the replicated object of prefab {}", prefabId.ToString());
                        if (root) {
                            root->Destroy();
                        }
                    } else {
                        std::vector<NetworkObject*> parts;
                        CollectObjects(*root, parts);
                        rootObject->m_id = id;
                        m_state->ids[rootObject] = id;
                        Object rootEntry{.object = rootObject, .entity = root->GetId(), .prefab = prefabId};
                        std::size_t next = 0;
                        for (NetworkObject* part : parts) {
                            if (part == rootObject || next >= children.size()) {
                                continue;
                            }
                            const NetworkId partId = children[next++];
                            part->m_id = partId;
                            m_state->ids[part] = partId;
                            m_state->objects[partId] = {
                                .object = part, .entity = part->GetEntity().GetId(), .root = id};
                            rootEntry.children.push_back(partId);
                            ApplyAuthority(*part);
                        }
                        m_state->objects[id] = std::move(rootEntry);
                        ApplyAuthority(*rootObject);
                    }
                }
            }
        }
        const bool owned = reader.ReadBool();
        const std::vector<std::byte> state = ReadBlob(reader);
        NetworkObject* object = reader.IsValid() ? FindObject(id) : nullptr;
        if (!object) {
            continue;
        }
        if (object->m_ownedLocally != owned) {
            object->m_ownedLocally = owned;
            ApplyAuthority(*object);
        }
        if (!object->HasAuthority()) {
            ReadState(*object, state, time);
        }
    }
    m_statistics.objects = m_state->objects.size();
}

void NetworkSystem::ClientSend(float deltaTime) {
    if (!m_session) {
        return;
    }
    const NetPeerId server = m_session->GetServerPeer();
    if (server == NetPeerId::Invalid) {
        return;
    }
    if (m_state->ackDue) {
        m_state->ackDue = false;
        BitWriter writer;
        writer.WriteBits(static_cast<std::uint32_t>(UnreliableKind::Ack), 8);
        writer.WriteBits(m_state->lastReceived, 32);
        writer.WriteBits(m_state->receivedMask, 32);
        m_session->GetHost().Send(server, NetworkSession::UnreliableChannel, writer.TakeBytes(),
                                  NetDelivery::Unreliable);
    }

    m_state->sendTimer += deltaTime;
    const float interval = 1.0f / std::max(m_config.sendRate, 1.0f);
    if (m_state->sendTimer < interval) {
        return;
    }
    m_state->sendTimer = std::fmod(m_state->sendTimer, interval);
    for (auto& [id, entry] : m_state->objects) {
        if (!entry.object || entry.object->authority != NetworkAuthority::Owner || !entry.object->HasAuthority()) {
            continue;
        }
        BitWriter writer;
        writer.WriteBits(static_cast<std::uint32_t>(UnreliableKind::OwnerState), 8);
        writer.WriteVarUInt(static_cast<std::uint32_t>(id));
        const std::vector<std::byte> state = WriteState(*entry.object);
        writer.WriteVarUInt(state.size());
        writer.WriteBytes(state);
        m_session->GetHost().Send(server, NetworkSession::UnreliableChannel, writer.TakeBytes(),
                                  NetDelivery::Sequenced);
    }
}

namespace Network {

NetworkSystem* Get(const Scene& scene) {
    NetworkSystem* system = scene.GetSystem<NetworkSystem>();
    return system && system->GetSession() ? system : nullptr;
}

bool IsServer(const Scene& scene) {
    const NetworkSystem* system = Get(scene);
    return !system || system->IsServer();
}

bool IsClient(const Scene& scene) {
    const NetworkSystem* system = Get(scene);
    return system && system->IsClient();
}

NetworkObject* GetObject(const Entity& entity) {
    for (const Entity* current = &entity; current; current = current->GetParent()) {
        if (auto* object = current->GetComponent<NetworkObject>()) {
            return object;
        }
    }
    return nullptr;
}

bool HasAuthority(const Entity& entity) {
    const NetworkObject* object = GetObject(entity);
    if (!object) {
        return true;
    }
    return object->HasAuthority();
}

} // namespace Network

} // namespace Spot
