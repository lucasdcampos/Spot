#pragma once

#include "spot/net/net_transport.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Spot {

struct NetHostConfig {
    // What both ends must agree on, such as the game's build or the version of its messages; a client of another
    // protocol is refused.
    std::uint32_t protocol = 0;
    // The channels of the game's messages, at most 254.
    std::size_t channels = 1;
    // Seconds a connection has to introduce itself before it is dropped.
    float handshakeTimeout = 10.0f;
};

struct NetEvent {
    enum class Type { Connected, Disconnected, Received };

    Type type = Type::Received;
    NetPeerId peer = NetPeerId::Invalid;
    std::uint8_t channel = 0;
    // Received: the message. Connected, on a server: what the client introduced itself with.
    std::vector<std::byte> data;
    // Disconnected: why, as the other end or the transport told it.
    std::string reason;
};

// The connections of a game, over a transport: a server, which clients connect to, or a client of one server. Each
// connection starts with a handshake: the client introduces itself, and the server accepts it or refuses it with a
// reason the client is told. Games send their own messages on their own channels.
class NetHost {
public:
    explicit NetHost(std::unique_ptr<NetTransport> transport, NetHostConfig config = {});
    // Tells every peer the host is going away.
    ~NetHost();

    NetHost(const NetHost&) = delete;
    NetHost& operator=(const NetHost&) = delete;

    // Receives connections on the port, or any free one with 0. False when it cannot.
    bool Listen(std::uint16_t port, std::size_t maxPeers);
    // Connects to a server; Poll() gives Connected once it accepted, or Disconnected with the reason. The hello is
    // what the server's onConnecting is given, such as a player's name and a password.
    NetPeerId Connect(const NetAddress& address, std::span<const std::byte> hello = {});

    // On a server, decides whether a client may connect, from what it introduced itself with: an empty string accepts
    // it, anything else refuses it, and is the reason it is told. Without one, every client is accepted.
    std::function<std::string(NetPeerId peer, std::span<const std::byte> hello)> onConnecting;

    // To a connected peer; ignored for others.
    void Send(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data, NetDelivery delivery);
    void SendToAll(std::uint8_t channel, std::span<const std::byte> data, NetDelivery delivery);
    // Tells the peer why, then disconnects it once what was sent reliably arrived.
    void Disconnect(NetPeerId peer, std::string_view reason = {});

    // Sends what is due, and gives what happened since the last call.
    std::vector<NetEvent> Poll();

    bool IsServer() const { return m_server; }
    bool IsConnected(NetPeerId peer) const;
    // The connected peers, in the order they connected.
    std::vector<NetPeerId> GetPeers() const;
    std::uint16_t GetPort() const;
    NetPeerStats GetStats(NetPeerId peer) const;
    std::optional<NetAddress> GetAddress(NetPeerId peer) const;
    NetTransport& GetTransport() { return *m_transport; }

private:
    enum class PeerState { Connecting, Introduced, Connected, Leaving };

    struct Peer {
        PeerState state = PeerState::Connecting;
        // Whether it was ever connected, which a disconnect does not change.
        bool accepted = false;
        double since = 0.0;
        std::uint64_t order = 0;
        std::vector<std::byte> hello;
        std::string reason;
    };

    bool OpenTransport(std::uint16_t port, std::size_t maxPeers);
    void Receive(NetTransportEvent& event, std::vector<NetEvent>& events);
    void ReceiveControl(NetPeerId peer, Peer& state, std::span<const std::byte> data, std::vector<NetEvent>& events);
    void SendControl(NetPeerId peer, std::uint8_t kind, std::span<const std::byte> payload);
    double Now() const;

    std::unique_ptr<NetTransport> m_transport;
    NetHostConfig m_config;
    bool m_open = false;
    bool m_server = false;
    std::uint64_t m_nextOrder = 0;
    std::unordered_map<NetPeerId, Peer> m_peers;
};

} // namespace Spot
