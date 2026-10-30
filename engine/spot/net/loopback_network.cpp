#include "spot/net/loopback_network.h"

#include "spot/core/debug/log.h"

#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace Spot {

namespace Detail {

struct LoopbackMessage {
    enum class Kind { ConnectRequest, Accepted, Refused, Data, Disconnect };

    Kind kind = Kind::Data;
    std::uint32_t fromEndpoint = 0;
    // The sender's id of the connection, by which the receiver knows it.
    std::uint32_t fromPeer = 0;
    // For Accepted and Refused: the receiver's own id of the connection.
    std::uint32_t toPeer = 0;
    std::uint16_t fromPort = 0;
    std::uint8_t channel = 0;
    std::vector<std::byte> data;
};

struct LoopbackSocket {
    std::uint32_t id = 0;
    std::uint16_t port = 0;
    std::deque<NetDatagram> inbox;
};

struct LoopbackState {
    std::uint32_t nextEndpoint = 1;
    std::uint16_t nextPort = 50000;
    std::unordered_map<std::uint32_t, std::deque<LoopbackMessage>*> inboxes;
    // The endpoint receiving connections on each port.
    std::unordered_map<std::uint16_t, std::uint32_t> listeners;
    std::multimap<std::uint16_t, LoopbackSocket*> sockets;

    void Deliver(std::uint32_t endpoint, LoopbackMessage message) {
        if (const auto found = inboxes.find(endpoint); found != inboxes.end()) {
            found->second->push_back(std::move(message));
        }
    }

    bool IsPortUsed(std::uint16_t port) const { return listeners.contains(port) || sockets.contains(port); }

    std::uint16_t FreePort() {
        while (IsPortUsed(nextPort) || nextPort == 0) {
            ++nextPort;
        }
        return nextPort++;
    }
};

} // namespace Detail

namespace {

using Detail::LoopbackMessage;

class LoopbackTransport final : public NetTransport {
public:
    explicit LoopbackTransport(std::shared_ptr<Detail::LoopbackState> state) : m_state(std::move(state)) {}

    ~LoopbackTransport() override {
        if (m_endpoint == 0) {
            return;
        }
        for (const auto& [id, peer] : m_peers) {
            m_state->Deliver(peer.remoteEndpoint,
                             {.kind = LoopbackMessage::Kind::Disconnect, .fromEndpoint = m_endpoint, .fromPeer = id});
        }
        m_state->inboxes.erase(m_endpoint);
        if (const auto found = m_state->listeners.find(m_port);
            found != m_state->listeners.end() && found->second == m_endpoint) {
            m_state->listeners.erase(found);
        }
    }

    bool Open(const NetTransportConfig& config) override {
        if (m_endpoint != 0) {
            Log::Error("A loopback transport was opened twice");
            return false;
        }
        if (config.port != 0 && m_state->IsPortUsed(config.port)) {
            Log::Error("Loopback port {} is already in use", config.port);
            return false;
        }
        m_port = config.port != 0 ? config.port : m_state->FreePort();
        m_maxPeers = config.maxPeers;
        m_channels = config.channels;
        m_endpoint = m_state->nextEndpoint++;
        m_state->inboxes[m_endpoint] = &m_inbox;
        m_state->listeners[m_port] = m_endpoint;
        return true;
    }

    std::uint16_t GetPort() const override { return m_port; }

    NetPeerId Connect(const NetAddress& address) override {
        if (m_endpoint == 0 || m_peers.size() >= m_maxPeers) {
            return NetPeerId::Invalid;
        }
        const std::uint32_t id = m_nextPeer++;
        const auto listener = m_state->listeners.find(address.port);
        if (listener == m_state->listeners.end() || listener->second == m_endpoint) {
            // Nobody listens there: the attempt fails, as a real one would once it timed out.
            m_localEvents.push_back({.type = NetTransportEvent::Type::Disconnected, .peer = NetPeerId{id}});
            return NetPeerId{id};
        }
        m_peers[id] = {.remoteEndpoint = listener->second, .remotePort = address.port};
        m_state->Deliver(listener->second, {.kind = LoopbackMessage::Kind::ConnectRequest,
                                            .fromEndpoint = m_endpoint,
                                            .fromPeer = id,
                                            .fromPort = m_port});
        return NetPeerId{id};
    }

    void Send(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data, NetDelivery) override {
        const auto found = m_peers.find(static_cast<std::uint32_t>(peer));
        if (found == m_peers.end() || channel >= m_channels) {
            return;
        }
        found->second.stats.bytesSent += data.size();
        m_state->Deliver(found->second.remoteEndpoint, {.kind = LoopbackMessage::Kind::Data,
                                                        .fromEndpoint = m_endpoint,
                                                        .fromPeer = found->first,
                                                        .channel = channel,
                                                        .data = {data.begin(), data.end()}});
    }

    void Disconnect(NetPeerId peer) override {
        const auto found = m_peers.find(static_cast<std::uint32_t>(peer));
        if (found == m_peers.end()) {
            return;
        }
        m_state->Deliver(
            found->second.remoteEndpoint,
            {.kind = LoopbackMessage::Kind::Disconnect, .fromEndpoint = m_endpoint, .fromPeer = found->first});
        Forget(found->first);
        m_localEvents.push_back({.type = NetTransportEvent::Type::Disconnected, .peer = peer});
    }

    void Poll(std::vector<NetTransportEvent>& events) override {
        for (NetTransportEvent& event : m_localEvents) {
            events.push_back(std::move(event));
        }
        m_localEvents.clear();
        while (!m_inbox.empty()) {
            LoopbackMessage message = std::move(m_inbox.front());
            m_inbox.pop_front();
            Receive(std::move(message), events);
        }
    }

    NetPeerStats GetStats(NetPeerId peer) const override {
        const auto found = m_peers.find(static_cast<std::uint32_t>(peer));
        return found != m_peers.end() ? found->second.stats : NetPeerStats{};
    }

    std::optional<NetAddress> GetAddress(NetPeerId peer) const override {
        const auto found = m_peers.find(static_cast<std::uint32_t>(peer));
        if (found == m_peers.end()) {
            return std::nullopt;
        }
        return NetAddress{"loopback", found->second.remotePort};
    }

private:
    struct Peer {
        std::uint32_t remoteEndpoint = 0;
        std::uint32_t remotePeer = 0;
        std::uint16_t remotePort = 0;
        bool connected = false;
        NetPeerStats stats;
    };

    static std::uint64_t Key(std::uint32_t endpoint, std::uint32_t peer) {
        return (static_cast<std::uint64_t>(endpoint) << 32) | peer;
    }

    void Receive(LoopbackMessage message, std::vector<NetTransportEvent>& events) {
        switch (message.kind) {
        case LoopbackMessage::Kind::ConnectRequest: {
            if (m_peers.size() >= m_maxPeers) {
                m_state->Deliver(
                    message.fromEndpoint,
                    {.kind = LoopbackMessage::Kind::Refused, .fromEndpoint = m_endpoint, .toPeer = message.fromPeer});
                return;
            }
            const std::uint32_t id = m_nextPeer++;
            m_peers[id] = {.remoteEndpoint = message.fromEndpoint,
                           .remotePeer = message.fromPeer,
                           .remotePort = message.fromPort,
                           .connected = true};
            m_remote[Key(message.fromEndpoint, message.fromPeer)] = id;
            m_state->Deliver(message.fromEndpoint, {.kind = LoopbackMessage::Kind::Accepted,
                                                    .fromEndpoint = m_endpoint,
                                                    .fromPeer = id,
                                                    .toPeer = message.fromPeer});
            events.push_back({.type = NetTransportEvent::Type::Connected, .peer = NetPeerId{id}});
            return;
        }
        case LoopbackMessage::Kind::Accepted: {
            const auto found = m_peers.find(message.toPeer);
            if (found == m_peers.end()) {
                return;
            }
            found->second.remotePeer = message.fromPeer;
            found->second.connected = true;
            m_remote[Key(message.fromEndpoint, message.fromPeer)] = found->first;
            events.push_back({.type = NetTransportEvent::Type::Connected, .peer = NetPeerId{found->first}});
            return;
        }
        case LoopbackMessage::Kind::Refused:
            if (m_peers.contains(message.toPeer)) {
                Forget(message.toPeer);
                events.push_back({.type = NetTransportEvent::Type::Disconnected, .peer = NetPeerId{message.toPeer}});
            }
            return;
        case LoopbackMessage::Kind::Data: {
            const auto found = m_remote.find(Key(message.fromEndpoint, message.fromPeer));
            if (found == m_remote.end()) {
                return;
            }
            m_peers[found->second].stats.bytesReceived += message.data.size();
            events.push_back({.type = NetTransportEvent::Type::Received,
                              .peer = NetPeerId{found->second},
                              .channel = message.channel,
                              .data = std::move(message.data)});
            return;
        }
        case LoopbackMessage::Kind::Disconnect: {
            const auto found = m_remote.find(Key(message.fromEndpoint, message.fromPeer));
            if (found == m_remote.end()) {
                return;
            }
            const std::uint32_t id = found->second;
            Forget(id);
            events.push_back({.type = NetTransportEvent::Type::Disconnected, .peer = NetPeerId{id}});
            return;
        }
        }
    }

    void Forget(std::uint32_t id) {
        const auto found = m_peers.find(id);
        if (found == m_peers.end()) {
            return;
        }
        m_remote.erase(Key(found->second.remoteEndpoint, found->second.remotePeer));
        m_peers.erase(found);
    }

    std::shared_ptr<Detail::LoopbackState> m_state;
    std::uint32_t m_endpoint = 0;
    std::uint16_t m_port = 0;
    std::size_t m_maxPeers = 0;
    std::size_t m_channels = 0;
    std::uint32_t m_nextPeer = 1;
    std::map<std::uint32_t, Peer> m_peers;
    std::unordered_map<std::uint64_t, std::uint32_t> m_remote;
    std::deque<LoopbackMessage> m_inbox;
    std::vector<NetTransportEvent> m_localEvents;
};

class LoopbackDatagramSocket final : public NetDatagramSocket {
public:
    explicit LoopbackDatagramSocket(std::shared_ptr<Detail::LoopbackState> state) : m_state(std::move(state)) {}

    ~LoopbackDatagramSocket() override {
        for (auto it = m_state->sockets.begin(); it != m_state->sockets.end(); ++it) {
            if (it->second == &m_socket) {
                m_state->sockets.erase(it);
                break;
            }
        }
    }

    bool Open(std::uint16_t port) override {
        if (m_open) {
            Log::Error("A loopback socket was opened twice");
            return false;
        }
        if (port != 0 && m_state->listeners.contains(port)) {
            Log::Error("Loopback port {} is already in use", port);
            return false;
        }
        m_socket.port = port != 0 ? port : m_state->FreePort();
        m_socket.id = m_state->nextEndpoint++;
        m_state->sockets.emplace(m_socket.port, &m_socket);
        m_open = true;
        return true;
    }

    std::uint16_t GetPort() const override { return m_socket.port; }

    void SendTo(const NetAddress& address, std::span<const std::byte> data) override {
        if (!m_open) {
            return;
        }
        const auto found = m_state->sockets.find(address.port);
        if (found != m_state->sockets.end()) {
            found->second->inbox.push_back({GetAddress(), {data.begin(), data.end()}});
        }
    }

    void Broadcast(std::uint16_t port, std::span<const std::byte> data) override {
        if (!m_open) {
            return;
        }
        const auto [first, last] = m_state->sockets.equal_range(port);
        for (auto it = first; it != last; ++it) {
            it->second->inbox.push_back({GetAddress(), {data.begin(), data.end()}});
        }
    }

    void Receive(std::vector<NetDatagram>& datagrams) override {
        for (NetDatagram& datagram : m_socket.inbox) {
            datagrams.push_back(std::move(datagram));
        }
        m_socket.inbox.clear();
    }

private:
    // Sockets sharing a port are told apart by their hosts, as machines would be.
    NetAddress GetAddress() const { return {"loopback" + std::to_string(m_socket.id), m_socket.port}; }

    std::shared_ptr<Detail::LoopbackState> m_state;
    Detail::LoopbackSocket m_socket;
    bool m_open = false;
};

} // namespace

LoopbackNetwork::LoopbackNetwork() : m_state(std::make_shared<Detail::LoopbackState>()) {
}

LoopbackNetwork::~LoopbackNetwork() = default;

std::unique_ptr<NetTransport> LoopbackNetwork::CreateTransport() {
    return std::make_unique<LoopbackTransport>(m_state);
}

std::unique_ptr<NetDatagramSocket> LoopbackNetwork::CreateDatagramSocket() {
    return std::make_unique<LoopbackDatagramSocket>(m_state);
}

} // namespace Spot
