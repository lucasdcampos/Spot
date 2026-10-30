#pragma once

#include "spot/net/net_transport.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Spot {

// Answers the queries of NetDiscovery on the local network, as a server does to be found. What it answers is the
// game's: usually its name, its map, its players and the port its NetHost listens on.
class NetDiscoveryResponder {
public:
    // Only queries of the same protocol are answered (see NetHostConfig::protocol).
    NetDiscoveryResponder(std::unique_ptr<NetDatagramSocket> socket, std::uint32_t protocol);

    // False when the port cannot be had.
    bool Listen(std::uint16_t port);
    // Called for each query; keep it under 1000 bytes.
    std::function<std::vector<std::byte>()> answer;
    // Answers the queries that arrived.
    void Update();

private:
    std::unique_ptr<NetDatagramSocket> m_socket;
    std::uint32_t m_protocol;
};

struct NetDiscoveryAnswer {
    // The responder's address: its host, and the port it listens to queries on.
    NetAddress from;
    std::vector<std::byte> data;
    // Seconds from the query to its answer.
    float roundTrip = 0.0f;
};

// Finds the servers of a game on the local network, by asking every machine listening on a port.
class NetDiscovery {
public:
    NetDiscovery(std::unique_ptr<NetDatagramSocket> socket, std::uint32_t protocol);

    // Broadcasts a query; answers come in Update(). Asking again keeps the answers, refreshing them as they come.
    bool Query(std::uint16_t port);
    void Update();
    // One per responder, the latest it gave.
    const std::vector<NetDiscoveryAnswer>& GetAnswers() const { return m_answers; }
    void Clear() { m_answers.clear(); }

private:
    std::unique_ptr<NetDatagramSocket> m_socket;
    std::uint32_t m_protocol;
    bool m_open = false;
    std::uint32_t m_query = 0;
    double m_queryTime = 0.0;
    std::vector<NetDiscoveryAnswer> m_answers;
};

} // namespace Spot
