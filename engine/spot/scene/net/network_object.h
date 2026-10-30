#pragma once

#include "spot/net/net_transport.h"
#include "spot/scene/component.h"

#include <cstdint>

namespace Spot {

// Identifies a replicated object on every machine of a session; the server gives them. 0 is no object.
enum class NetworkId : std::uint32_t { Invalid = 0 };

// Who decides an object's state.
enum class NetworkAuthority {
    // The server simulates it; clients show it.
    Server,
    // The client owning it simulates it and sends its state, as for a vehicle its driver drives; the server and the
    // other clients show it. Without an owner, the server has authority.
    Owner,
};

// Makes its entity, and what is below it, one object kept in sync between the machines of a session (see
// NetworkSystem): its NetworkTransform and its Replicated components, on it and below it down to the next
// NetworkObject. Objects of the scene's file are matched between machines by the ids they were saved with; others must
// be made from a prefab whose root has a NetworkObject, so clients can make the same.
class NetworkObject final : public Component {
public:
    NetworkAuthority authority = NetworkAuthority::Server;
    // How much it matters to send this object's changes: twice the priority, twice as often when there is not room
    // for every change.
    float priority = 1.0f;
    // Beyond this distance from a player's focus (see NetworkSystem::SetFocus()), its priority falls with distance;
    // 0 keeps it the same anywhere.
    float relevanceRadius = 0.0f;

    NetworkId GetNetworkId() const { return m_id; }
    // The client owning it; Invalid when the server does.
    NetPeerId GetOwner() const { return m_owner; }
    // On clients, whether this one owns it.
    bool IsOwnedLocally() const { return m_ownedLocally; }
    // Whether this machine decides its state: with no session, always.
    bool HasAuthority() const;

    void Serialize(Archive& archive) override;

private:
    friend class NetworkSystem;

    NetworkId m_id = NetworkId::Invalid;
    NetPeerId m_owner = NetPeerId::Invalid;
    bool m_ownedLocally = false;
};

} // namespace Spot
