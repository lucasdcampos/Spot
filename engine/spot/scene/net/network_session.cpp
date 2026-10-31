#include "spot/scene/net/network_session.h"

#include "spot/scene/net/network_system.h"

#include <cmath>

namespace Spot {

namespace {

// Entity references without a bound scene know no entities.
class NoEntities final : public NetEntityMapper {
public:
    std::uint64_t ToWire(EntityId /*entity*/) const override { return 0; }
    EntityId FromWire(std::uint64_t /*id*/) const override { return EntityId::Invalid; }
};

const NoEntities s_noEntities;

} // namespace

NetworkSession::NetworkSession(std::unique_ptr<NetHost> host)
    : m_host(std::move(host)), m_start(std::chrono::steady_clock::now()) {
}

NetworkSession::~NetworkSession() = default;

bool NetworkSession::IsConnected() const {
    return IsServer() || (m_server != NetPeerId::Invalid && m_host->IsConnected(m_server));
}

void NetworkSession::Update() {
    for (NetEvent& event : m_host->Poll()) {
        switch (event.type) {
        case NetEvent::Type::Connected:
            if (!IsServer()) {
                m_server = event.peer;
            }
            OnPeerConnected.Emit(event.peer, event.data);
            break;
        case NetEvent::Type::Disconnected:
            if (m_system) {
                m_system->PeerDisconnected(event.peer);
            }
            if (!IsServer()) {
                m_server = NetPeerId::Invalid;
                m_hasTime = false;
            }
            OnPeerDisconnected.Emit(event.peer, event.reason);
            break;
        case NetEvent::Type::Received:
            if (event.channel == MessageChannel) {
                m_messages.Dispatch(event.peer, event.data, &GetEntityMapper());
            } else if (m_system) {
                m_system->Receive(event.peer, event.channel, event.data);
            }
            break;
        }
    }
}

double NetworkSession::GetLocalTime() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
}

double NetworkSession::GetTime() const {
    if (IsServer()) {
        return GetLocalTime();
    }
    return m_hasTime ? GetLocalTime() + m_timeOffset : 0.0;
}

void NetworkSession::ObserveServerTime(double serverTime) {
    // By arrival rather than by the round trip, which transports estimate slowly (ENet starts from half a second): the
    // clock follows the earliest arrivals, as late packets are only late, and drifts back slowly should the way get
    // longer. Eased, so the time objects are shown at never jumps with the jitter of one packet.
    const double offset = serverTime - GetLocalTime();
    if (!m_hasTime || std::abs(offset - m_timeOffset) > 1.0) {
        m_timeOffset = offset;
        m_hasTime = true;
        return;
    }
    m_timeOffset += (offset - m_timeOffset) * (offset > m_timeOffset ? 0.2 : 0.01);
}

const NetEntityMapper& NetworkSession::GetEntityMapper() const {
    return m_system ? static_cast<const NetEntityMapper&>(*m_system) : s_noEntities;
}

void NetworkSession::SendBytes(NetPeerId peer, std::span<const std::byte> data, NetDelivery delivery) {
    m_host->Send(peer, MessageChannel, data, delivery);
}

void NetworkSession::Bind(NetworkSystem* system) {
    m_system = system;
}

void NetworkSession::Unbind(NetworkSystem* system) {
    if (m_system == system) {
        m_system = nullptr;
    }
}

} // namespace Spot
