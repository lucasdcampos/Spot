#include "spot/net/net_conditions.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <unordered_map>

namespace Spot {

struct ConditionedNetTransport::State {
    struct Pending {
        double due = 0.0;
        std::uint64_t order = 0;
        NetPeerId peer = NetPeerId::Invalid;
        std::uint8_t channel = 0;
        NetDelivery delivery = NetDelivery::Reliable;
        std::vector<std::byte> data;
        bool disconnect = false;
    };

    std::function<double()> clock;
    std::mt19937 random;
    std::vector<Pending> pending;
    std::uint64_t nextOrder = 0;
    // When the last reliable message to each peer is due, so none overtakes another.
    std::unordered_map<NetPeerId, double> lastReliable;
    // The same for sequenced messages, by peer and channel, so none arrives after a newer one.
    std::unordered_map<std::uint64_t, double> lastSequenced;

    float Chance() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(random); }

    void Add(Pending item) {
        item.order = nextOrder++;
        pending.push_back(std::move(item));
    }
};

ConditionedNetTransport::ConditionedNetTransport(std::unique_ptr<NetTransport> transport, NetConditions conditions,
                                                 std::function<double()> clock, std::uint32_t seed)
    : m_transport(std::move(transport)), m_conditions(conditions), m_state(std::make_unique<State>()) {
    if (!clock) {
        clock = [start = std::chrono::steady_clock::now()] {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        };
    }
    m_state->clock = std::move(clock);
    m_state->random.seed(seed);
}

ConditionedNetTransport::~ConditionedNetTransport() = default;

bool ConditionedNetTransport::Open(const NetTransportConfig& config) {
    return m_transport->Open(config);
}

std::uint16_t ConditionedNetTransport::GetPort() const {
    return m_transport->GetPort();
}

NetPeerId ConditionedNetTransport::Connect(const NetAddress& address) {
    return m_transport->Connect(address);
}

void ConditionedNetTransport::Send(NetPeerId peer, std::uint8_t channel, std::span<const std::byte> data,
                                   NetDelivery delivery) {
    if (m_conditions.IsPerfect() && m_state->pending.empty()) {
        m_transport->Send(peer, channel, data, delivery);
        return;
    }
    const double now = m_state->clock();
    const auto delay = [&] {
        return std::max(0.0f, m_conditions.latency) + std::max(0.0f, m_conditions.jitter) * m_state->Chance();
    };
    if (delivery == NetDelivery::Reliable) {
        double& last = m_state->lastReliable[peer];
        last = std::max(last, now + delay());
        m_state->Add(
            {.due = last, .peer = peer, .channel = channel, .delivery = delivery, .data = {data.begin(), data.end()}});
        return;
    }
    if (m_state->Chance() < m_conditions.loss) {
        return;
    }
    const int copies = delivery == NetDelivery::Unreliable && m_state->Chance() < m_conditions.duplicates ? 2 : 1;
    for (int copy = 0; copy < copies; ++copy) {
        double due = now + delay();
        if (delivery == NetDelivery::Sequenced) {
            double& last = m_state->lastSequenced[(static_cast<std::uint64_t>(peer) << 8) | channel];
            due = last = std::max(last, due);
        }
        m_state->Add(
            {.due = due, .peer = peer, .channel = channel, .delivery = delivery, .data = {data.begin(), data.end()}});
    }
}

void ConditionedNetTransport::Disconnect(NetPeerId peer) {
    if (m_state->pending.empty()) {
        m_transport->Disconnect(peer);
        return;
    }
    // After what was sent reliably before it.
    double& last = m_state->lastReliable[peer];
    last = std::max(last, m_state->clock());
    m_state->Add({.due = last, .peer = peer, .disconnect = true});
}

void ConditionedNetTransport::Poll(std::vector<NetTransportEvent>& events) {
    const double now = m_state->clock();
    std::vector<State::Pending> due;
    std::erase_if(m_state->pending, [&](State::Pending& item) {
        if (item.due > now) {
            return false;
        }
        due.push_back(std::move(item));
        return true;
    });
    std::sort(due.begin(), due.end(), [](const State::Pending& a, const State::Pending& b) {
        return a.due != b.due ? a.due < b.due : a.order < b.order;
    });
    for (const State::Pending& item : due) {
        if (item.disconnect) {
            m_transport->Disconnect(item.peer);
        } else {
            m_transport->Send(item.peer, item.channel, item.data, item.delivery);
        }
    }
    const std::size_t first = events.size();
    m_transport->Poll(events);
    for (std::size_t i = first; i < events.size(); ++i) {
        if (events[i].type == NetTransportEvent::Type::Disconnected) {
            const NetPeerId peer = events[i].peer;
            m_state->lastReliable.erase(peer);
            std::erase_if(m_state->lastSequenced,
                          [peer](const auto& entry) { return (entry.first >> 8) == static_cast<std::uint64_t>(peer); });
            std::erase_if(m_state->pending, [peer](const State::Pending& item) { return item.peer == peer; });
        }
    }
}

NetPeerStats ConditionedNetTransport::GetStats(NetPeerId peer) const {
    NetPeerStats stats = m_transport->GetStats(peer);
    // Both ends usually simulate the same conditions, so a round trip takes this end's latency twice.
    stats.roundTrip += 2.0f * (std::max(0.0f, m_conditions.latency) + 0.5f * std::max(0.0f, m_conditions.jitter));
    stats.jitter += std::max(0.0f, m_conditions.jitter);
    stats.loss = std::clamp(stats.loss + m_conditions.loss, 0.0f, 1.0f);
    return stats;
}

std::optional<NetAddress> ConditionedNetTransport::GetAddress(NetPeerId peer) const {
    return m_transport->GetAddress(peer);
}

} // namespace Spot
