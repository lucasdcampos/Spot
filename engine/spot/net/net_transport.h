#pragma once

#include "spot/net/net_address.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Spot {

// A machine connected to this one through a transport. Ids are never reused by the same transport.
enum class NetPeerId : std::uint32_t { Invalid = 0 };

// How a message gets there.
enum class NetDelivery {
    // Always, and in the order sent on its channel.
    Reliable,
    // Maybe, in any order.
    Unreliable,
    // Maybe, but never after a newer one of its channel, as for state where only the latest matters.
    Sequenced,
};

struct NetPeerStats {
    // In seconds, as the transport measures them.
    float roundTrip = 0.0f;
    float jitter = 0.0f;
    // The share of packets lost, from 0 to 1.
    float loss = 0.0f;
    std::uint64_t bytesSent = 0;
    std::uint64_t bytesReceived = 0;
};

struct NetTransportEvent {
    enum class Type { Connected, Disconnected, Received };

    Type type = Type::Received;
    NetPeerId peer = NetPeerId::Invalid;
    std::uint8_t channel = 0;
    std::vector<std::byte> data;
};

struct NetTransportConfig {
    // The port to receive connections on; 0 for any free one, as clients do.
    std::uint16_t port = 0;
    std::size_t maxPeers = 1;
    // Messages of different channels are ordered apart, so a slow one never holds up the others. At most 255.
    std::size_t channels = 1;
};

// Connections between machines, as the platform makes them: the contract of the engine's network backends, which
// NetHost builds games' connections on. Every function is called from one thread.
class NetTransport {
public:
    virtual ~NetTransport() = default;

    // False, with an error logged, when the port cannot be had.
    virtual bool Open(const NetTransportConfig& config) = 0;
    // The port it receives on, once open.
    virtual std::uint16_t GetPort() const = 0;
    // Starts connecting; Poll() tells when the peer is connected or could not be. Invalid when the address cannot be
    // resolved or the transport is full.
    virtual NetPeerId Connect(const NetAddress& address) = 0;
    // To a peer connecting or connected; ignored for others. The data is copied.
    virtual void Send(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data, NetDelivery delivery) = 0;
    // Once what was sent reliably is delivered; both ends then get a Disconnected event.
    virtual void Disconnect(NetPeerId peer) = 0;
    // Sends what is due, and appends what happened since the last call.
    virtual void Poll(std::vector<NetTransportEvent>& events) = 0;
    virtual NetPeerStats GetStats(NetPeerId peer) const = 0;
    virtual std::optional<NetAddress> GetAddress(NetPeerId peer) const = 0;
};

// A message to or from any machine, without a connection, as for finding servers on the local network.
struct NetDatagram {
    NetAddress from;
    std::vector<std::byte> data;
};

// Messages without connections, delivered maybe, each in one packet: keep them under 1200 bytes.
class NetDatagramSocket {
public:
    virtual ~NetDatagramSocket() = default;

    // On a port, or any free one with 0. Several sockets may share a port, and each gets its broadcasts.
    virtual bool Open(std::uint16_t port) = 0;
    virtual std::uint16_t GetPort() const = 0;
    virtual void SendTo(const NetAddress& address, std::span<const std::byte> data) = 0;
    // To every machine of the local network listening on the port.
    virtual void Broadcast(std::uint16_t port, std::span<const std::byte> data) = 0;
    // Appends what arrived since the last call.
    virtual void Receive(std::vector<NetDatagram>& datagrams) = 0;
};

// The platform's transport over UDP, and its sockets. Addresses are IPv4 for now.
std::unique_ptr<NetTransport> CreateNetTransport();
std::unique_ptr<NetDatagramSocket> CreateNetDatagramSocket();

} // namespace Spot
