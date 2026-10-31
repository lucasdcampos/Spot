#include "spot/core/debug/log.h"
#include "spot/net/net_transport.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <enet/enet.h>

#include <array>
#include <unordered_map>

namespace Spot {

namespace {

// ENet is initialized while any of its hosts or sockets exist.
class ENetUser {
public:
    ENetUser() {
        if (s_users++ == 0 && enet_initialize() != 0) {
            Log::Error("The network could not be initialized");
        }
    }
    ~ENetUser() {
        if (--s_users == 0) {
            enet_deinitialize();
        }
    }

    ENetUser(const ENetUser&) = delete;
    ENetUser& operator=(const ENetUser&) = delete;

private:
    static inline int s_users = 0;
};

std::optional<ENetAddress> Resolve(const NetAddress& address) {
    ENetAddress resolved{};
    if (enet_address_set_host(&resolved, address.host.c_str()) != 0) {
        Log::Warning("Could not find the address of {}", address.host);
        return std::nullopt;
    }
    resolved.port = address.port;
    return resolved;
}

NetAddress ToNetAddress(const ENetAddress& address) {
    std::array<char, 64> host{};
    if (enet_address_get_host_ip(&address, host.data(), host.size()) != 0) {
        host = {};
    }
    return {host.data(), address.port};
}

class ENetTransport final : public NetTransport {
public:
    ~ENetTransport() override {
        if (!m_host) {
            return;
        }
        // What was sent goes out, then the peers are told at once, as the host will not be there to finish.
        enet_host_flush(m_host);
        for (const auto& [id, peer] : m_peers) {
            enet_peer_disconnect_now(peer.peer, 0);
        }
        enet_host_destroy(m_host);
    }

    bool Open(const NetTransportConfig& config) override {
        if (m_host) {
            Log::Error("A network transport was opened twice");
            return false;
        }
        ENetAddress address{ENET_HOST_ANY, config.port};
        m_host = enet_host_create(&address, config.maxPeers, config.channels, 0, 0);
        if (!m_host) {
            Log::Error("Could not open port {} for the network", config.port);
            return false;
        }
        ENetAddress bound{};
        m_port = enet_socket_get_address(m_host->socket, &bound) == 0 ? bound.port : config.port;
        m_channels = config.channels;
        return true;
    }

    std::uint16_t GetPort() const override { return m_port; }

    NetPeerId Connect(const NetAddress& address) override {
        if (!m_host) {
            return NetPeerId::Invalid;
        }
        const std::optional<ENetAddress> resolved = Resolve(address);
        if (!resolved) {
            return NetPeerId::Invalid;
        }
        ENetPeer* peer = enet_host_connect(m_host, &*resolved, m_channels, 0);
        return peer ? Add(peer) : NetPeerId::Invalid;
    }

    void Send(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data, NetDelivery delivery) override {
        const auto found = m_peers.find(peer);
        if (found == m_peers.end() || channel >= m_channels) {
            return;
        }
        enet_uint32 flags = 0;
        if (delivery == NetDelivery::Reliable) {
            flags = ENET_PACKET_FLAG_RELIABLE;
        } else if (delivery == NetDelivery::Unreliable) {
            flags = ENET_PACKET_FLAG_UNSEQUENCED | ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
        } else {
            flags = ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
        }
        ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
        if (packet && enet_peer_send(found->second.peer, channel, packet) != 0) {
            enet_packet_destroy(packet);
            return;
        }
        found->second.stats.bytesSent += data.size();
    }

    void Disconnect(NetPeerId peer) override {
        const auto found = m_peers.find(peer);
        if (found != m_peers.end()) {
            enet_peer_disconnect_later(found->second.peer, 0);
        }
    }

    void Poll(std::vector<NetTransportEvent>& events) override {
        if (!m_host) {
            return;
        }
        ENetEvent event{};
        while (enet_host_service(m_host, &event, 0) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                const NetPeerId id = event.peer->data ? IdOf(event.peer) : Add(event.peer);
                events.push_back({.type = NetTransportEvent::Type::Connected, .peer = id});
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                if (event.peer->data) {
                    const NetPeerId id = IdOf(event.peer);
                    m_peers.erase(id);
                    event.peer->data = nullptr;
                    events.push_back({.type = NetTransportEvent::Type::Disconnected, .peer = id});
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                if (event.peer->data) {
                    const NetPeerId id = IdOf(event.peer);
                    const auto* bytes = reinterpret_cast<const std::byte*>(event.packet->data);
                    m_peers[id].stats.bytesReceived += event.packet->dataLength;
                    events.push_back({.type = NetTransportEvent::Type::Received,
                                      .peer = id,
                                      .channel = event.channelID,
                                      .data = {bytes, bytes + event.packet->dataLength}});
                }
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
        enet_host_flush(m_host);
    }

    NetPeerStats GetStats(NetPeerId peer) const override {
        const auto found = m_peers.find(peer);
        if (found == m_peers.end()) {
            return {};
        }
        NetPeerStats stats = found->second.stats;
        const ENetPeer& enetPeer = *found->second.peer;
        stats.roundTrip = static_cast<float>(enetPeer.roundTripTime) / 1000.0f;
        stats.jitter = static_cast<float>(enetPeer.roundTripTimeVariance) / 1000.0f;
        stats.loss = static_cast<float>(enetPeer.packetLoss) / static_cast<float>(ENET_PEER_PACKET_LOSS_SCALE);
        stats.throttle =
            static_cast<float>(enetPeer.packetThrottle) / static_cast<float>(ENET_PEER_PACKET_THROTTLE_SCALE);
        return stats;
    }

    std::optional<NetAddress> GetAddress(NetPeerId peer) const override {
        const auto found = m_peers.find(peer);
        if (found == m_peers.end()) {
            return std::nullopt;
        }
        return ToNetAddress(found->second.peer->address);
    }

private:
    struct Peer {
        ENetPeer* peer = nullptr;
        NetPeerStats stats;
    };

    NetPeerId Add(ENetPeer* peer) {
        const NetPeerId id{m_nextPeer++};
        // The id is kept in the peer's own data, as ENet reuses its peers.
        peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
        m_peers[id] = {.peer = peer};
        return id;
    }

    static NetPeerId IdOf(const ENetPeer* peer) {
        return static_cast<NetPeerId>(reinterpret_cast<std::uintptr_t>(peer->data));
    }

    ENetUser m_user;
    ENetHost* m_host = nullptr;
    std::uint16_t m_port = 0;
    std::size_t m_channels = 1;
    std::uint32_t m_nextPeer = 1;
    std::unordered_map<NetPeerId, Peer> m_peers;
};

class ENetDatagramSocket final : public NetDatagramSocket {
public:
    ~ENetDatagramSocket() override {
        if (m_socket != ENET_SOCKET_NULL) {
            enet_socket_destroy(m_socket);
        }
    }

    bool Open(std::uint16_t port) override {
        if (m_socket != ENET_SOCKET_NULL) {
            Log::Error("A network socket was opened twice");
            return false;
        }
        m_socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if (m_socket == ENET_SOCKET_NULL) {
            Log::Error("Could not make a network socket");
            return false;
        }
        enet_socket_set_option(m_socket, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(m_socket, ENET_SOCKOPT_BROADCAST, 1);
        enet_socket_set_option(m_socket, ENET_SOCKOPT_REUSEADDR, 1);
        ENetAddress address{ENET_HOST_ANY, port};
        if (enet_socket_bind(m_socket, &address) != 0) {
            Log::Error("Could not open port {} for the network", port);
            enet_socket_destroy(m_socket);
            m_socket = ENET_SOCKET_NULL;
            return false;
        }
        ENetAddress bound{};
        m_port = enet_socket_get_address(m_socket, &bound) == 0 ? bound.port : port;
        return true;
    }

    std::uint16_t GetPort() const override { return m_port; }

    void SendTo(const NetAddress& address, std::span<const std::byte> data) override {
        if (const std::optional<ENetAddress> resolved = Resolve(address)) {
            SendTo(*resolved, data);
        }
    }

    void Broadcast(std::uint16_t port, std::span<const std::byte> data) override {
        SendTo(ENetAddress{ENET_HOST_BROADCAST, port}, data);
    }

    void Receive(std::vector<NetDatagram>& datagrams) override {
        if (m_socket == ENET_SOCKET_NULL) {
            return;
        }
        std::array<std::byte, 1500> buffer{};
        for (;;) {
            ENetAddress from{};
            // Its fields are in another order on each platform.
            ENetBuffer view{};
            view.data = buffer.data();
            view.dataLength = buffer.size();
            const int received = enet_socket_receive(m_socket, &from, &view, 1);
            if (received <= 0) {
                break;
            }
            datagrams.push_back({ToNetAddress(from), {buffer.begin(), buffer.begin() + received}});
        }
    }

private:
    void SendTo(const ENetAddress& address, std::span<const std::byte> data) {
        if (m_socket == ENET_SOCKET_NULL) {
            return;
        }
        ENetBuffer view{};
        view.data = const_cast<std::byte*>(data.data());
        view.dataLength = data.size();
        enet_socket_send(m_socket, &address, &view, 1);
    }

    ENetUser m_user;
    ENetSocket m_socket = ENET_SOCKET_NULL;
    std::uint16_t m_port = 0;
};

} // namespace

std::unique_ptr<NetTransport> CreateNetTransport() {
    return std::make_unique<ENetTransport>();
}

std::unique_ptr<NetDatagramSocket> CreateNetDatagramSocket() {
    return std::make_unique<ENetDatagramSocket>();
}

} // namespace Spot
