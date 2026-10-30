#include "spot/net/net_host.h"

#include "spot/core/debug/log.h"
#include "spot/net/bit_stream.h"

#include <algorithm>
#include <chrono>

namespace Spot {

namespace {

// The transport's channel 0 carries the handshake; the game's channels come after it.
constexpr std::uint8_t ControlChannel = 0;
constexpr std::uint32_t Magic = 0x544F5053; // "SPOT"

constexpr std::uint8_t Hello = 1;
constexpr std::uint8_t Welcome = 2;
constexpr std::uint8_t Goodbye = 3;

} // namespace

NetHost::NetHost(std::unique_ptr<NetTransport> transport, NetHostConfig config)
    : m_transport(std::move(transport)), m_config(config) {
    m_config.channels = std::clamp<std::size_t>(m_config.channels, 1, 254);
}

NetHost::~NetHost() {
    const std::string_view reason = m_server ? "The server shut down" : "The player left";
    for (auto& [peer, state] : m_peers) {
        if (state.state != PeerState::Leaving) {
            Disconnect(peer, reason);
        }
    }
    std::vector<NetTransportEvent> ignored;
    m_transport->Poll(ignored);
}

bool NetHost::OpenTransport(std::uint16_t port, std::size_t maxPeers) {
    if (m_open) {
        Log::Error("A network host can only listen or connect once");
        return false;
    }
    m_open = m_transport->Open({.port = port, .maxPeers = maxPeers, .channels = m_config.channels + 1});
    return m_open;
}

bool NetHost::Listen(std::uint16_t port, std::size_t maxPeers) {
    if (!OpenTransport(port, maxPeers)) {
        return false;
    }
    m_server = true;
    return true;
}

NetPeerId NetHost::Connect(const NetAddress& address, std::span<const std::byte> hello) {
    if (!OpenTransport(0, 1)) {
        return NetPeerId::Invalid;
    }
    const NetPeerId peer = m_transport->Connect(address);
    if (peer != NetPeerId::Invalid) {
        m_peers[peer] = {.since = Now(), .order = m_nextOrder++, .hello = {hello.begin(), hello.end()}};
    }
    return peer;
}

void NetHost::Send(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data, NetDelivery delivery) {
    if (channel >= m_config.channels || !IsConnected(peer)) {
        return;
    }
    m_transport->Send(peer, static_cast<std::uint8_t>(channel + 1), data, delivery);
}

void NetHost::SendToAll(std::uint8_t channel, std::span<const std::byte> data, NetDelivery delivery) {
    for (const NetPeerId peer : GetPeers()) {
        Send(peer, channel, data, delivery);
    }
}

void NetHost::Disconnect(NetPeerId peer, std::string_view reason) {
    const auto found = m_peers.find(peer);
    if (found == m_peers.end() || found->second.state == PeerState::Leaving) {
        return;
    }
    BitWriter writer;
    writer.WriteString(reason);
    SendControl(peer, Goodbye, writer.TakeBytes());
    found->second.reason = reason;
    found->second.state = PeerState::Leaving;
    m_transport->Disconnect(peer);
}

std::vector<NetEvent> NetHost::Poll() {
    std::vector<NetEvent> events;
    if (!m_open) {
        return events;
    }
    if (m_server) {
        const double now = Now();
        for (auto& [peer, state] : m_peers) {
            if (state.state == PeerState::Connecting && now - state.since > m_config.handshakeTimeout) {
                Disconnect(peer, "The connection did not introduce itself in time");
            }
        }
    }
    std::vector<NetTransportEvent> received;
    m_transport->Poll(received);
    for (NetTransportEvent& event : received) {
        Receive(event, events);
    }
    return events;
}

void NetHost::Receive(NetTransportEvent& event, std::vector<NetEvent>& events) {
    switch (event.type) {
    case NetTransportEvent::Type::Connected:
        if (m_server) {
            m_peers[event.peer] = {.since = Now(), .order = m_nextOrder++};
        } else if (const auto found = m_peers.find(event.peer); found != m_peers.end()) {
            BitWriter writer;
            writer.WriteBits(Magic, 32);
            writer.WriteBits(m_config.protocol, 32);
            writer.WriteBytes(found->second.hello);
            SendControl(event.peer, Hello, writer.TakeBytes());
            found->second.state = PeerState::Introduced;
        }
        return;
    case NetTransportEvent::Type::Disconnected: {
        const auto found = m_peers.find(event.peer);
        if (found == m_peers.end()) {
            return;
        }
        Peer peer = std::move(found->second);
        m_peers.erase(found);
        // A server tells only of the clients it had accepted; a client always learns how its connection ended.
        if (m_server && !peer.accepted) {
            return;
        }
        if (peer.reason.empty()) {
            peer.reason = peer.accepted ? "The connection was lost" : "Could not connect to the server";
        }
        events.push_back({.type = NetEvent::Type::Disconnected, .peer = event.peer, .reason = std::move(peer.reason)});
        return;
    }
    case NetTransportEvent::Type::Received: {
        const auto found = m_peers.find(event.peer);
        if (found == m_peers.end()) {
            return;
        }
        if (event.channel == ControlChannel) {
            ReceiveControl(event.peer, found->second, event.data, events);
        } else if (found->second.state == PeerState::Connected) {
            events.push_back({.type = NetEvent::Type::Received,
                              .peer = event.peer,
                              .channel = static_cast<std::uint8_t>(event.channel - 1),
                              .data = std::move(event.data)});
        }
        return;
    }
    }
}

void NetHost::ReceiveControl(NetPeerId peer, Peer& state, std::span<const std::byte> data,
                             std::vector<NetEvent>& events) {
    BitReader reader(data);
    const std::uint32_t kind = reader.ReadBits(8);
    if (kind == Hello && m_server && state.state == PeerState::Connecting) {
        const std::uint32_t magic = reader.ReadBits(32);
        const std::uint32_t protocol = reader.ReadBits(32);
        if (!reader.IsValid() || magic != Magic) {
            Disconnect(peer, "Not a client of this game");
            return;
        }
        if (protocol != m_config.protocol) {
            Disconnect(peer, "The server runs another version of the game");
            return;
        }
        std::vector<std::byte> hello = reader.ReadBytes(reader.GetBitsLeft() / 8);
        std::string refusal = onConnecting ? onConnecting(peer, hello) : std::string();
        if (!refusal.empty()) {
            Disconnect(peer, refusal);
            return;
        }
        SendControl(peer, Welcome, {});
        state.state = PeerState::Connected;
        state.accepted = true;
        events.push_back({.type = NetEvent::Type::Connected, .peer = peer, .data = std::move(hello)});
    } else if (kind == Welcome && !m_server && state.state == PeerState::Introduced) {
        state.state = PeerState::Connected;
        state.accepted = true;
        events.push_back({.type = NetEvent::Type::Connected, .peer = peer});
    } else if (kind == Goodbye) {
        std::string reason = reader.ReadString();
        if (reader.IsValid() && !reason.empty()) {
            state.reason = std::move(reason);
        }
    }
}

void NetHost::SendControl(NetPeerId peer, std::uint8_t kind, std::span<const std::byte> payload) {
    std::vector<std::byte> message;
    message.reserve(payload.size() + 1);
    message.push_back(static_cast<std::byte>(kind));
    message.insert(message.end(), payload.begin(), payload.end());
    m_transport->Send(peer, ControlChannel, message, NetDelivery::Reliable);
}

bool NetHost::IsConnected(NetPeerId peer) const {
    const auto found = m_peers.find(peer);
    return found != m_peers.end() && found->second.state == PeerState::Connected;
}

std::vector<NetPeerId> NetHost::GetPeers() const {
    std::vector<std::pair<std::uint64_t, NetPeerId>> connected;
    for (const auto& [peer, state] : m_peers) {
        if (state.state == PeerState::Connected) {
            connected.emplace_back(state.order, peer);
        }
    }
    std::sort(connected.begin(), connected.end());
    std::vector<NetPeerId> peers;
    peers.reserve(connected.size());
    for (const auto& entry : connected) {
        peers.push_back(entry.second);
    }
    return peers;
}

std::uint16_t NetHost::GetPort() const {
    return m_transport->GetPort();
}

NetPeerStats NetHost::GetStats(NetPeerId peer) const {
    return m_transport->GetStats(peer);
}

std::optional<NetAddress> NetHost::GetAddress(NetPeerId peer) const {
    return m_transport->GetAddress(peer);
}

double NetHost::Now() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace Spot
