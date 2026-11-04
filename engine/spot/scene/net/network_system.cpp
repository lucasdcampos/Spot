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

enum class ReliableKind : std::uint32_t { SceneReady = 1, ObjectMessage = 2, SceneObjects = 3, Placement = 4 };
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

// What a client is told of its hold on an object: none, it owns it, or it owns and decides it.
constexpr std::uint8_t NotOwned = 0;
constexpr std::uint8_t Owned = 1;
constexpr std::uint8_t OwnedAndDecided = 2;

std::uint8_t GetOwnership(const NetworkObject& object, NetPeerId peer) {
    if (object.GetOwner() != peer) {
        return NotOwned;
    }
    return object.authority == NetworkAuthority::Owner ? OwnedAndDecided : Owned;
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

// How far in the past to show what another machine decides, from how its states arrive: a state must be ahead of what
// is shown until the next comes, so the delay is the time between them, the peak of how late they came over the last
// second, and a little more for the frames that receive them. A hitch raises it at once; a second after, it falls as
// quickly, with what is shown moving at most half again as fast as time meanwhile.
class ShownDelay {
public:
    // A state made at that time arrived now. States of the same time, as those of several objects, count once for
    // the time between them, and late ones not at all.
    void Observe(double stateTime, double now) {
        if (stateTime > m_lastStateTime) {
            const double interval = stateTime - m_lastStateTime;
            if (m_lastStateTime > 0.0 && interval < 1.0) {
                m_interval += (interval - m_interval) * 0.1;
            }
            m_lastStateTime = stateTime;
        }
        const double late = std::clamp(now - stateTime, 0.0, 1.0);
        while (!m_lateness.empty() && m_lateness.front().first < now - 1.0) {
            m_lateness.pop_front();
        }
        // Each entry later than every one after it: the front is the peak.
        while (!m_lateness.empty() && m_lateness.back().second <= late) {
            m_lateness.pop_back();
        }
        m_lateness.emplace_back(now, late);
        const double wanted = std::min(m_interval * 1.5 + m_lateness.front().second + 0.01, 1.0);
        if (m_delay < 0.0 || wanted > m_delay) {
            m_delay = m_delay < 0.0 ? wanted : m_delay + (wanted - m_delay) * 0.3;
        } else {
            const double elapsed = m_lastObserved > 0.0 ? std::max(now - m_lastObserved, 0.0) : 0.0;
            m_delay -= std::min((m_delay - wanted) * 0.3, elapsed * 0.5);
        }
        m_lastObserved = now;
    }

    // Negative before any state arrived.
    double Get() const { return m_delay; }

private:
    double m_lastStateTime = 0.0;
    double m_interval = 0.0;
    double m_lastObserved = 0.0;
    // When states arrived and how late, falling from the front.
    std::deque<std::pair<double, double>> m_lateness;
    double m_delay = -1.0;
};

// What owners stamp their states with, on the server's clock: when the earliest of them arrive, so only the jitter of
// the way between remains as lateness. It follows earlier arrivals at once and later ones slowly, as the clients'
// estimate of the server's clock does.
class OwnerClock {
public:
    // States of the same time, as an owner's objects sent together, keep the same time.
    double ToServerTime(double ownerTime, double arrival) {
        if (m_known && ownerTime == m_lastOwnerTime) {
            return ownerTime + m_offset;
        }
        m_lastOwnerTime = ownerTime;
        const double offset = arrival - ownerTime;
        if (!m_known || std::abs(offset - m_offset) > 1.0) {
            m_offset = offset;
            m_known = true;
        } else {
            m_offset += (offset - m_offset) * (offset < m_offset ? 0.2 : 0.01);
        }
        return ownerTime + m_offset;
    }

private:
    double m_offset = 0.0;
    double m_lastOwnerTime = 0.0;
    bool m_known = false;
};

class ResolvingEntityMapper final : public NetEntityMapper {
public:
    ResolvingEntityMapper(const NetEntityMapper& mapper, std::vector<std::uint64_t>& unresolved)
        : m_mapper(mapper), m_unresolved(unresolved) {}

    std::uint64_t ToWire(EntityId entity) const override { return m_mapper.ToWire(entity); }
    EntityId FromWire(std::uint64_t id) const override {
        const EntityId entity = m_mapper.FromWire(id);
        if (id != 0 && entity == EntityId::Invalid) {
            m_unresolved.push_back(id);
        }
        return entity;
    }

private:
    const NetEntityMapper& m_mapper;
    std::vector<std::uint64_t>& m_unresolved;
};

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
    // Roots spawned below another object, as a lamp put on a prop: the object they are on.
    NetworkId parent = NetworkId::Invalid;
    // On the server: the state written for the snapshots being sent.
    std::vector<std::byte> state;
    std::uint64_t revision = 0;
    bool wasChanging = false;
    // On clients: the latest state is retained only while it refers to objects not spawned here yet.
    std::vector<std::uint64_t> unresolved;
    // On clients: whether it was put where the server had it, as it must be once even when this machine decides it.
    bool placed = false;
    double stateTime = 0.0;
    // Its Replicated parts, found again when components were added or removed since.
    std::vector<Replicated*> parts;
    std::uint64_t partsGeneration = 0;
    // On the server, while a client decides it: the transform that client sent last, and when it had it on the
    // server's clock, sent on to the others as it came rather than as the server shows it.
    std::vector<std::byte> ownerTransform;
    double ownerTime = 0.0;
    // While a client decides it: how many times the server put it somewhere, which that client's states carry; the
    // server ignores those sent before the latest.
    std::uint32_t placement = 0;
    // On the client deciding it: the state it sent last, how many times in a row, and when.
    std::vector<std::byte> sentState;
    std::uint32_t sentRepeats = 0;
    double sentTime = 0.0;
};

struct NetworkSystem::Peer {
    struct Known {
        std::uint32_t ackedSequence = 0;
        bool spawned = false;
        std::uint8_t ackedOwned = 0;
        bool sent = false;
        std::uint8_t lastSentOwned = 0;
        std::uint64_t lastSentRevision = 0;
        std::uint32_t lastChangeSequence = 0;
        std::uint64_t ackedRevision = 0;
        float accumulator = 0.0f;
        // Seconds since it was last sent to this client, by the snapshots' time.
        float sinceSent = 1000.0f;
    };
    struct Despawn {
        bool byKey = false;
        std::uint64_t value = 0;

        friend bool operator==(const Despawn&, const Despawn&) = default;
    };
    struct Sent {
        NetworkId id = NetworkId::Invalid;
        std::uint8_t owned = 0;
        std::uint64_t revision = 0;
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
    // The share of the snapshot budget this client gets, less while what it is sent is being lost.
    float budgetScale = 1.0f;
    // What the client decides, shown on the server.
    OwnerClock ownerClock;
    ShownDelay ownerDelay;
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
    // On the server: snapshots made so far, by which bodies at rest take turns being read.
    std::uint32_t sends = 0;
    // On clients: whether the server was told which scene this one runs.
    bool toldScene = false;
    // On clients: the snapshots received, to acknowledge.
    std::uint32_t lastReceived = 0;
    std::uint32_t receivedMask = 0;
    bool ackDue = false;
    // On clients: how far back what the server and other clients decide is shown.
    ShownDelay shownDelay;
    // The session's time when the frame's fixed steps began, and the time of the last state sent: what the fixed
    // steps made, as the frame's remainder is simulated by the next.
    double frameTime = 0.0;
    double stateTime = 0.0;
    // Counts additions and removals of components, which may change the parts of objects.
    std::uint64_t partsGeneration = 1;
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
    m_announced = false;
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
    const auto give = [&](NetworkObject& given) {
        // What the last owner decided is not the new one's.
        if (const auto found = m_state->objects.find(given.GetNetworkId());
            found != m_state->objects.end() && given.m_owner != owner) {
            found->second.ownerTransform.clear();
        }
        given.m_owner = owner;
        ApplyAuthority(given);
    };
    give(object);
    const auto found = m_state->ids.find(&object);
    if (found == m_state->ids.end()) {
        return;
    }
    for (const NetworkId child : m_state->objects[found->second].children) {
        if (NetworkObject* nested = FindObject(child)) {
            give(*nested);
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
    return m_session->GetTime() - GetShownDelay();
}

double NetworkSystem::GetShownDelay() const {
    return ClampShownDelay(m_state->shownDelay.Get());
}

double NetworkSystem::GetShownDelay(NetPeerId owner) const {
    const auto found = m_state->peers.find(owner);
    return ClampShownDelay(found != m_state->peers.end() ? found->second.ownerDelay.Get() : -1.0);
}

double NetworkSystem::ClampShownDelay(double delay) const {
    const double least = std::max(static_cast<double>(m_config.interpolationDelay), 0.0);
    return std::max(least, std::min(delay, static_cast<double>(m_config.maxInterpolationDelay)));
}

double NetworkSystem::GetStateTime() {
    m_state->stateTime =
        std::max(m_state->stateTime, m_state->frameTime - static_cast<double>(GetScene().GetFixedTimeLeft()));
    return m_state->stateTime;
}

const std::vector<Replicated*>& NetworkSystem::GetParts(NetworkObject& object) {
    const auto found = m_state->objects.find(object.GetNetworkId());
    if (found == m_state->objects.end() || found->second.object != &object) {
        static thread_local std::vector<Replicated*> loose;
        loose.clear();
        CollectReplicated(object.GetEntity(), true, loose);
        return loose;
    }
    Object& entry = found->second;
    if (entry.partsGeneration != m_state->partsGeneration) {
        entry.parts.clear();
        CollectReplicated(object.GetEntity(), true, entry.parts);
        entry.partsGeneration = m_state->partsGeneration;
    }
    return entry.parts;
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
    ++m_state->partsGeneration;
    if (m_bound && IsServer() && dynamic_cast<NetworkObject*>(&component)) {
        m_state->added.push_back(component.GetEntity().GetId());
    }
}

void NetworkSystem::OnComponentRemoved(Component& component) {
    ++m_state->partsGeneration;
    if (auto* object = dynamic_cast<NetworkObject*>(&component)) {
        Forget(*object);
    }
}

void NetworkSystem::OnEntityDestroyed(Entity& entity) {
    ++m_state->partsGeneration;
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
    writer.WriteVarUInt(m_session->GetSceneGeneration());
    m_session->GetHost().Send(m_session->GetServerPeer(), NetworkSession::ReplicationChannel, writer.TakeBytes(),
                              NetDelivery::Reliable);
}

void NetworkSystem::TellSceneAgain() {
    m_state->toldScene = false;
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
        m_state->frameTime = m_session->GetTime();
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

    const double now = m_session->GetTime();
    const double shown = GetShownTime();
    m_statistics.shownObjects = 0;
    m_statistics.lateObjects = 0;
    for (auto& [id, entry] : m_state->objects) {
        if (entry.object && !entry.object->HasAuthority()) {
            if (auto* transform = entry.object->GetEntity().GetComponent<NetworkTransform>()) {
                // On the server, what a client decides, as far back as that client's states need.
                const double at = IsServer() ? now - GetShownDelay(entry.object->GetOwner()) : shown;
                ++m_statistics.shownObjects;
                m_statistics.lateObjects += transform->ShowAt(at) ? 1 : 0;
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
        const bool kinematic = made->second;
        m_state->kinematic.erase(made);
        // It goes on from where its newest state puts it now, not from where it was shown, in the past, which the
        // others would see it jump back to; and as it was moving, as a car a driver gets into at speed.
        auto* transform = object.GetEntity().GetComponent<NetworkTransform>();
        if (transform && m_session && m_session->HasTime()) {
            transform->ShowNewest(m_session->GetTime());
        }
        body->SetKinematic(kinematic);
        if (transform && !kinematic) {
            body->SetLinearVelocity(transform->GetShownVelocity());
        }
    }
    if (authority) {
        if (auto* transform = object.GetEntity().GetComponent<NetworkTransform>()) {
            transform->m_samples.clear();
        }
        if (const auto found = m_state->ids.find(&object); found != m_state->ids.end()) {
            m_state->objects[found->second].ownerTransform.clear();
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
        const std::uint64_t generation = reader.ReadVarUInt();
        // Once for each scene: starting over would have the whole world sent again each time a client asks.
        if (IsServer() && reader.IsValid() && scene == GetScene().GetSourceAsset() &&
            generation == m_session->GetSceneGeneration() && !IsPeerReady(peer)) {
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
    if (reliable && kind == static_cast<std::uint32_t>(ReliableKind::Placement)) {
        if (IsClient()) {
            ClientReceivePlacement(reader);
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

std::vector<std::byte> NetworkSystem::WriteState(NetworkObject& object, bool fromOwner) {
    BitWriter writer;
    Entity& entity = object.GetEntity();
    const auto entry = m_state->objects.find(object.GetNetworkId());
    if (auto* transform = entity.GetComponent<NetworkTransform>()) {
        writer.WriteBool(true);
        if (!fromOwner && entry != m_state->objects.end() && !entry->second.ownerTransform.empty()) {
            writer.WriteVarUInt(entry->second.ownerTransform.size());
            writer.WriteBytes(entry->second.ownerTransform);
        } else {
            BitWriter blob;
            transform->Write(blob);
            WriteBlob(writer, blob);
        }
    } else {
        writer.WriteBool(false);
    }
    const std::vector<Replicated*>& replicated = GetParts(object);
    writer.WriteVarUInt(replicated.size());
    for (Replicated* part : replicated) {
        BitWriter blob;
        // Empty, as the server does not read it.
        if (!fromOwner || part->IsDecidedByOwner()) {
            NetArchiveWriter backend(blob, this);
            Archive archive(backend);
            part->Replicate(archive);
        }
        WriteBlob(writer, blob);
    }
    return writer.TakeBytes();
}

void NetworkSystem::ReadState(NetworkObject& object, std::span<const std::byte> state, double time, bool fromOwner) {
    const NetworkId id = object.GetNetworkId();
    std::vector<std::uint64_t> unresolved;
    const ResolvingEntityMapper entities(*this, unresolved);
    BitReader reader(state);
    Entity& entity = object.GetEntity();
    // A client deciding the object moves it itself, once it was put where the server had it.
    const bool decidesHere = IsClient() && object.HasAuthority();
    const auto entry = m_state->objects.find(id);
    const bool placed = entry != m_state->objects.end() && entry->second.placed;
    if (reader.ReadBool()) {
        const std::vector<std::byte> blob = ReadBlob(reader);
        if (auto* transform = entity.GetComponent<NetworkTransform>();
            transform && reader.IsValid() && !(decidesHere && placed)) {
            if (entry != m_state->objects.end() && IsClient()) {
                entry->second.placed = true;
            }
            if (fromOwner && entry != m_state->objects.end()) {
                entry->second.ownerTransform = blob;
                entry->second.ownerTime = time;
            }
            BitReader transformReader(blob);
            const bool first = transform->m_samples.empty();
            transform->Read(transformReader, time);
            // A new object starts where it is, rather than moving there from where it was made.
            if (first) {
                transform->ShowAt(time);
            }
        }
    }
    // A copy, as applying a part may add or remove components.
    const std::vector<Replicated*> replicated = GetParts(object);
    const std::uint64_t count = reader.ReadVarUInt();
    for (std::uint64_t i = 0; i < count && reader.IsValid(); ++i) {
        const std::vector<std::byte> blob = ReadBlob(reader);
        if (!reader.IsValid() || i >= replicated.size()) {
            continue;
        }
        // The owner's parts go from it to the others, never back.
        const bool ownerDecides = replicated[i]->IsDecidedByOwner();
        if (fromOwner ? !ownerDecides : ownerDecides && decidesHere) {
            continue;
        }
        BitReader partReader(blob);
        NetArchiveReader backend(partReader, &entities);
        Archive archive(backend);
        replicated[i]->Replicate(archive);
        if (partReader.IsValid()) {
            replicated[i]->OnReplicated();
        }
    }
    if (IsClient()) {
        const auto found = m_state->objects.find(id);
        if (found != m_state->objects.end()) {
            found->second.state =
                unresolved.empty() ? std::vector<std::byte>() : std::vector<std::byte>(state.begin(), state.end());
            found->second.unresolved = std::move(unresolved);
            found->second.stateTime = time;
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
            // The topmost object above it not yet replicated is the one spawned, those below are its parts, and the
            // nearest replicated one above, if any, is what it was put on.
            Entity* rootEntity = entity;
            NetworkObject* parentObject = nullptr;
            for (Entity* parent = entity->GetParent(); parent; parent = parent->GetParent()) {
                if (auto* above = parent->GetComponent<NetworkObject>()) {
                    if (m_state->ids.contains(above)) {
                        parentObject = above;
                        break;
                    }
                    rootEntity = parent;
                }
            }
            NetworkObject* root = rootEntity->GetComponent<NetworkObject>();
            if (m_state->ids.contains(root)) {
                continue;
            }
            if (!rootEntity->GetPrefabAssetId().IsValid()) {
                Log::Warning("'{}' was not made from a prefab, so clients cannot make it", rootEntity->GetName());
                continue;
            }
            std::vector<NetworkObject*> parts;
            CollectObjects(*rootEntity, parts);
            const NetworkId rootId{m_state->nextId++};
            Object rootEntry{.object = root,
                             .entity = rootEntity->GetId(),
                             .prefab = rootEntity->GetPrefabAssetId(),
                             .parent = parentObject ? m_state->ids[parentObject] : NetworkId::Invalid};
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

    ++m_state->sends;
    for (auto& [id, entry] : m_state->objects) {
        if (entry.object) {
            // What rests rarely changes, and reading the whole world each time is what a large room costs most. What
            // else it has, as a color, comes at most a few snapshots late.
            constexpr std::uint32_t RestingTurns = 8;
            const auto* body = entry.object->GetEntity().GetComponent<Rigidbody>();
            if (!entry.wasChanging && body && !body->IsKinematic() && body->IsSleeping() &&
                (m_state->sends + static_cast<std::uint32_t>(id)) % RestingTurns != 0) {
                continue;
            }
            std::vector<std::byte> state = WriteState(*entry.object);
            const bool changed = state != entry.state;
            // Clients infer velocity from transform samples. They need one identical sample after motion stops,
            // even when they already acknowledged this pose, or extrapolation leaves them permanently ahead.
            if (changed || (entry.wasChanging && entry.object->GetEntity().HasComponent<NetworkTransform>())) {
                ++entry.revision;
            }
            entry.wasChanging = changed;
            entry.state = std::move(state);
        }
    }

    const double time = GetStateTime();
    for (auto& [peerId, peer] : m_state->peers) {
        if (!peer.ready || !m_session->GetHost().IsConnected(peerId)) {
            continue;
        }
        const Entity* focus = GetScene().GetEntity(peer.focus);
        const float resendAfter =
            std::clamp(m_session->GetHost().GetStats(peerId).roundTrip * 1.5f, interval * 2.0f, 0.5f);
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
            const std::uint8_t owned = GetOwnership(*entry.object, peerId);
            if (known.spawned && known.ackedSequence >= known.lastChangeSequence && known.ackedOwned == owned &&
                known.ackedRevision == entry.revision) {
                continue;
            }
            float scale = 1.0f;
            const float radius = entry.object->relevanceRadius;
            if (focus && radius > 0.0f) {
                const float distance = Distance(focus->GetTransform().GetWorldPosition(),
                                                entry.object->GetEntity().GetTransform().GetWorldPosition());
                scale = radius / std::max(radius, distance);
            }
            known.sinceSent += elapsed;
            // Beyond its radius it is sent less often, down to an eighth; and a change this client has on its way is
            // not sent again until its acknowledgement should have come.
            const bool seen = known.spawned && known.lastSentOwned == owned;
            if (seen && known.sinceSent < interval / std::max(scale, 0.125f) - interval * 0.5f) {
                continue;
            }
            if (known.sent && known.lastSentOwned == owned && known.lastSentRevision == entry.revision &&
                known.sinceSent < resendAfter) {
                continue;
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

        // Less while the way to this client loses what it is sent, and back slowly: what does not fit waits.
        const float loss = m_session->GetHost().GetStats(peerId).loss;
        peer.budgetScale = loss > 0.05f ? std::max(peer.budgetScale * 0.98f, 0.25f)
                                        : std::min(peer.budgetScale + 0.002f, 1.0f);
        const auto budget = static_cast<std::size_t>(static_cast<float>(m_config.snapshotBudget) * peer.budgetScale);

        BitWriter entries;
        std::size_t count = 0;
        std::unordered_set<NetworkId> written;
        for (const Candidate& candidate : candidates) {
            if (entries.GetBitCount() / 8 >= budget) {
                break;
            }
            Object& entry = m_state->objects[candidate.id];
            Peer::Known& known = peer.known[candidate.id];
            // A part needs its root on the client first, and what was put on another object, that object.
            if (entry.root != NetworkId::Invalid && !peer.known[entry.root].spawned && !written.contains(entry.root)) {
                continue;
            }
            if (entry.parent != NetworkId::Invalid && !peer.known[entry.parent].spawned &&
                !written.contains(entry.parent)) {
                continue;
            }
            const std::uint8_t owned = GetOwnership(*entry.object, peerId);
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
                    entries.WriteVarUInt(static_cast<std::uint32_t>(entry.parent));
                }
            }
            entries.WriteBits(owned, 2);
            // What a client decides goes on with the time it had it, so the others move it as it moved.
            const bool ownTime = !entry.ownerTransform.empty();
            entries.WriteBool(ownTime);
            if (ownTime) {
                entries.WriteFloat(static_cast<float>(std::clamp(time - entry.ownerTime, 0.0, 1.0)));
            }
            entries.WriteVarUInt(entry.state.size());
            entries.WriteBytes(entry.state);
            known.sent = true;
            // The hold may return to the acknowledged one after an unacknowledged change. Keep sending that return
            // until it too is acknowledged, since the client may have seen the change and lost its ACK.
            if (known.lastSentOwned != owned || known.lastSentRevision != entry.revision) {
                known.lastChangeSequence = snapshot.sequence;
                known.lastSentOwned = owned;
                known.lastSentRevision = entry.revision;
            }
            known.accumulator = 0.0f;
            known.sinceSent = 0.0f;
            written.insert(candidate.id);
            snapshot.sent.push_back({.id = candidate.id, .owned = owned, .revision = entry.revision});
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
            known->second.ackedRevision = sent.revision;
        }
        for (const Peer::Despawn& despawn : snapshot.despawns) {
            std::erase(peer.pendingDespawns, despawn);
        }
    }
}

void NetworkSystem::ServerReceiveOwnerState(NetPeerId peer, BitReader& reader) {
    const auto id = NetworkId{static_cast<std::uint32_t>(reader.ReadVarUInt())};
    const double ownerTime = reader.ReadDouble();
    const auto placement = static_cast<std::uint32_t>(reader.ReadVarUInt());
    const std::vector<std::byte> state = ReadBlob(reader);
    NetworkObject* object = reader.IsValid() && std::isfinite(ownerTime) ? FindObject(id) : nullptr;
    const auto found = m_state->peers.find(peer);
    if (!object || object->authority != NetworkAuthority::Owner || object->GetOwner() != peer ||
        found == m_state->peers.end() || m_state->objects[id].placement != placement) {
        return;
    }
    const double now = m_session->GetTime();
    const double time = found->second.ownerClock.ToServerTime(ownerTime, now);
    found->second.ownerDelay.Observe(time, now);
    ReadState(*object, state, time, true);
}

void NetworkSystem::Place(NetworkObject& object) {
    const auto found = m_state->objects.find(object.GetNetworkId());
    if (!IsServer() || !m_session || object.HasAuthority() || found == m_state->objects.end()) {
        return;
    }
    Object& entry = found->second;
    ++entry.placement;
    entry.ownerTransform.clear();
    BitWriter writer;
    writer.WriteBits(static_cast<std::uint32_t>(ReliableKind::Placement), 8);
    writer.WriteVarUInt(static_cast<std::uint32_t>(object.GetNetworkId()));
    writer.WriteVarUInt(entry.placement);
    if (auto* transform = object.GetEntity().GetComponent<NetworkTransform>()) {
        // Shown where it was put until the owner's states from there arrive.
        transform->m_samples.clear();
        writer.WriteBool(true);
        transform->Write(writer);
    } else {
        writer.WriteBool(false);
    }
    m_session->GetHost().Send(object.GetOwner(), NetworkSession::ReplicationChannel, writer.TakeBytes(),
                              NetDelivery::Reliable);
}

void NetworkSystem::ClientReceivePlacement(BitReader& reader) {
    const auto id = NetworkId{static_cast<std::uint32_t>(reader.ReadVarUInt())};
    const auto placement = static_cast<std::uint32_t>(reader.ReadVarUInt());
    const bool moved = reader.ReadBool();
    const auto found = m_state->objects.find(id);
    if (!reader.IsValid() || found == m_state->objects.end() || !found->second.object) {
        return;
    }
    found->second.placement = placement;
    auto* transform = found->second.object->GetEntity().GetComponent<NetworkTransform>();
    if (moved && transform) {
        // Even before this machine hears it decides the object: the states it has are from before.
        transform->m_samples.clear();
        transform->Place(reader);
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
    if (!reader.IsValid() || !std::isfinite(time)) {
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
    if (m_session->HasTime()) {
        m_state->shownDelay.Observe(time, m_session->GetTime());
    }
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

    struct Pending {
        NetworkId id;
        std::uint8_t owned;
        std::vector<std::byte> state;
        double time;
    };
    std::vector<Pending> pending;
    const double now = m_session->GetTime();
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
                const auto parentId = NetworkId{static_cast<std::uint32_t>(reader.ReadVarUInt())};
                if (reader.IsValid() && !FindObject(id)) {
                    AssetRef<Prefab> prefab = m_config.loadPrefab ? m_config.loadPrefab(prefabId) : AssetRef<Prefab>();
                    if (!prefab && Assets::GetManager()) {
                        prefab = Assets::GetManager()->Load<Prefab>(prefabId);
                    }
                    Entity* above = FindEntity(parentId);
                    Entity* root = prefab ? GetScene().Instantiate(prefab, {.parent = above}) : nullptr;
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
                        Object rootEntry{
                            .object = rootObject, .entity = root->GetId(), .prefab = prefabId, .parent = parentId};
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
        const auto owned = static_cast<std::uint8_t>(reader.ReadBits(2));
        const double stateTime = reader.ReadBool() ? time - static_cast<double>(reader.ReadFloat()) : time;
        std::vector<std::byte> state = ReadBlob(reader);
        if (reader.IsValid()) {
            // What another client decides comes later than the rest, and must be shown far enough back too.
            if (stateTime != time && owned != OwnedAndDecided) {
                m_state->shownDelay.Observe(stateTime, now);
            }
            pending.push_back({id, owned, std::move(state), stateTime});
        }
    }
    // Every spawn in this snapshot must be registered before fields that may refer to it are read.
    for (const Pending& entry : pending) {
        NetworkObject* object = FindObject(entry.id);
        if (!object) {
            continue;
        }
        // What it owns, the server may also have it decide, for a while, as a prop its player pushes.
        const bool owned = entry.owned != 0;
        const NetworkAuthority authority = entry.owned == OwnedAndDecided ? NetworkAuthority::Owner
                                           : owned                        ? NetworkAuthority::Server
                                                                          : object->authority;
        if (object->m_ownedLocally != owned || object->authority != authority) {
            object->m_ownedLocally = owned;
            object->authority = authority;
            ApplyAuthority(*object);
        }
        // Also what this machine decides of it: the server still decides the rest, as who rides it.
        ReadState(*object, entry.state, entry.time);
    }
    ClientResolveReferences();
    m_statistics.objects = m_state->objects.size();
}

void NetworkSystem::ClientResolveReferences() {
    struct Pending {
        NetworkId id;
        std::vector<std::byte> state;
        double time;
    };
    std::vector<Pending> pending;
    for (const auto& [id, entry] : m_state->objects) {
        if (entry.object && !entry.unresolved.empty() &&
            std::ranges::all_of(entry.unresolved,
                                [this](std::uint64_t reference) { return FromWire(reference) != EntityId::Invalid; })) {
            pending.push_back({id, entry.state, entry.stateTime});
        }
    }
    // A reference may have been sent in an earlier snapshot whose state the server already considers acknowledged.
    for (const Pending& entry : pending) {
        if (NetworkObject* object = FindObject(entry.id)) {
            ReadState(*object, entry.state, entry.time);
        }
    }
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
    // When it had them, by its estimate of the server's clock, for the server to space them as they were made.
    const double time = GetStateTime();
    bool sentAny = false;
    for (auto& [id, entry] : m_state->objects) {
        if (!entry.object || entry.object->authority != NetworkAuthority::Owner || !entry.object->HasAuthority()) {
            continue;
        }
        // Where it is and what the owner decides: what else it has, as its health or who rides it, stays the
        // server's.
        std::vector<std::byte> state = WriteState(*entry.object, true);
        // At rest, a prop given to this machine costs nothing but a state now and then. It stops being sent only after
        // two the same, as the server infers velocity from them; and one object always goes, by which the server
        // knows how often states come and how far back to show them.
        entry.sentRepeats = state == entry.sentState ? entry.sentRepeats + 1 : 0;
        if (entry.sentRepeats >= 2 && sentAny && time - entry.sentTime < 0.5) {
            continue;
        }
        sentAny = true;
        entry.sentTime = time;
        BitWriter writer;
        writer.WriteBits(static_cast<std::uint32_t>(UnreliableKind::OwnerState), 8);
        writer.WriteVarUInt(static_cast<std::uint32_t>(id));
        writer.WriteDouble(time);
        writer.WriteVarUInt(entry.placement);
        writer.WriteVarUInt(state.size());
        writer.WriteBytes(state);
        m_session->GetHost().Send(server, NetworkSession::UnreliableChannel, writer.TakeBytes(),
                                  NetDelivery::Sequenced);
        entry.sentState = std::move(state);
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
