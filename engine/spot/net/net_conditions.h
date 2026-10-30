#pragma once

#include "spot/net/net_transport.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace Spot {

// How bad a simulated network is, for what this end sends.
struct NetConditions {
    // Seconds each message takes, one way.
    float latency = 0.0f;
    // Seconds up to which each message takes more, at random.
    float jitter = 0.0f;
    // The share of unreliable and sequenced messages lost, from 0 to 1. Reliable ones are never lost.
    float loss = 0.0f;
    // The share of unreliable messages sent twice.
    float duplicates = 0.0f;

    bool IsPerfect() const { return latency <= 0.0f && jitter <= 0.0f && loss <= 0.0f && duplicates <= 0.0f; }
};

// A transport that sends through another under simulated conditions, to see how a game plays over a bad network
// without one. Reliable messages keep their order; others may come in any order when there is jitter.
class ConditionedNetTransport final : public NetTransport {
public:
    // The clock gives seconds; by default the time since the program started. The seed makes runs repeatable.
    explicit ConditionedNetTransport(std::unique_ptr<NetTransport> transport, NetConditions conditions = {},
                                     std::function<double()> clock = {}, std::uint32_t seed = 1);
    ~ConditionedNetTransport() override;

    const NetConditions& GetConditions() const { return m_conditions; }
    void SetConditions(const NetConditions& conditions) { m_conditions = conditions; }
    NetTransport& GetInner() { return *m_transport; }

    bool Open(const NetTransportConfig& config) override;
    std::uint16_t GetPort() const override;
    NetPeerId Connect(const NetAddress& address) override;
    void Send(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data, NetDelivery delivery) override;
    void Disconnect(NetPeerId peer) override;
    void Poll(std::vector<NetTransportEvent>& events) override;
    NetPeerStats GetStats(NetPeerId peer) const override;
    std::optional<NetAddress> GetAddress(NetPeerId peer) const override;

private:
    struct State;

    std::unique_ptr<NetTransport> m_transport;
    NetConditions m_conditions;
    std::unique_ptr<State> m_state;
};

} // namespace Spot
