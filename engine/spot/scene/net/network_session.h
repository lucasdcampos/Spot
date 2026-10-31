#pragma once

#include "spot/net/net_host.h"
#include "spot/net/net_messages.h"
#include "spot/scene/signal.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace Spot {

class NetworkSystem;

enum class NetworkRole { Server, Client };

// A game's connections between machines: a server and its clients, or a client and its server, over a NetHost made
// with ChannelCount channels. It outlives scenes; the scene it plays is kept in sync by the NetworkSystem bound to
// it. Games send their own messages through it, and hear players join and leave.
class NetworkSession {
public:
    static constexpr std::size_t ChannelCount = 4;
    // The game's messages, then the channels of replication.
    static constexpr std::uint8_t MessageChannel = 0;
    static constexpr std::uint8_t ReplicationChannel = 1;
    static constexpr std::uint8_t SnapshotChannel = 2;
    static constexpr std::uint8_t UnreliableChannel = 3;

    // The host already listens, for a server, or connects, for a client.
    explicit NetworkSession(std::unique_ptr<NetHost> host);
    ~NetworkSession();

    NetworkSession(const NetworkSession&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;

    NetworkRole GetRole() const { return m_host->IsServer() ? NetworkRole::Server : NetworkRole::Client; }
    bool IsServer() const { return m_host->IsServer(); }
    NetHost& GetHost() { return *m_host; }
    const NetHost& GetHost() const { return *m_host; }
    // On a client, its connection to the server, once connected; Invalid on servers.
    NetPeerId GetServerPeer() const { return m_server; }
    // A server always is; a client once its server accepted it, until it is disconnected.
    bool IsConnected() const;
    // The bound scene's system, if any.
    NetworkSystem* GetSystem() const { return m_system; }

    // Polls the host: tells of peers joining and leaving, gives messages to their handlers and replication to the
    // bound system. Call it every frame, as NetworkModule does; more often is harmless.
    void Update();

    // Handlers of the game's messages; on clients, the peer is the server's.
    NetMessageHandlers& GetMessages() { return m_messages; }
    template <NetMessage T>
    void Send(NetPeerId peer, T message, NetDelivery delivery = NetDelivery::Reliable) {
        SendBytes(peer, Encode(message), delivery);
    }
    // From a server to every client, or from a client to its server.
    template <NetMessage T>
    void SendToAll(T message, NetDelivery delivery = NetDelivery::Reliable) {
        const std::vector<std::byte> data = Encode(message);
        for (const NetPeerId peer : m_host->GetPeers()) {
            SendBytes(peer, data, delivery);
        }
    }
    template <NetMessage T>
    void SendToServer(T message, NetDelivery delivery = NetDelivery::Reliable) {
        if (m_server != NetPeerId::Invalid) {
            SendBytes(m_server, Encode(message), delivery);
        }
    }

    // A peer connected, with what it introduced itself with; on a client, the server accepted it.
    Signal<NetPeerId, std::span<const std::byte>> OnPeerConnected;
    // A peer left, or, on a client, the connection to the server ended, with the reason.
    Signal<NetPeerId, std::string_view> OnPeerDisconnected;

    // Seconds on the server's clock since it started: what clients show replicated objects by. On clients, the time of
    // the server's newest snapshots as they arrive, which trails the server by the time they take to arrive: what is
    // shown a little before it then always has states on both sides. Before the first snapshot, it is 0.
    double GetTime() const;
    // On clients: whether the server's time has been heard yet.
    bool HasTime() const { return IsServer() || m_hasTime; }

private:
    friend class NetworkSystem;

    // Entity references cross as the bound system's ids.
    template <NetMessage T>
    std::vector<std::byte> Encode(T& message) const {
        BitWriter writer;
        WriteNetMessage(writer, message, &GetEntityMapper());
        return writer.TakeBytes();
    }
    const NetEntityMapper& GetEntityMapper() const;
    void SendBytes(NetPeerId peer, std::span<const std::byte> data, NetDelivery delivery);
    void Bind(NetworkSystem* system);
    void Unbind(NetworkSystem* system);
    // A server's time, as it was when it sent something just received.
    void ObserveServerTime(double serverTime);
    double GetLocalTime() const;

    std::unique_ptr<NetHost> m_host;
    NetworkSystem* m_system = nullptr;
    NetMessageHandlers m_messages;
    NetPeerId m_server = NetPeerId::Invalid;
    std::chrono::steady_clock::time_point m_start;
    double m_timeOffset = 0.0;
    bool m_hasTime = false;
    bool m_disconnected = false;
};

} // namespace Spot
