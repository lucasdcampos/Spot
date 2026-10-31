#include "spot/scene/net/network_object.h"

#include "spot/scene/net/network_system.h"
#include "spot/scene/scene.h"

namespace Spot {

namespace {

constexpr std::string_view AuthorityNames[] = {"Server", "Owner"};

} // namespace

bool NetworkObject::HasAuthority() const {
    const NetworkSystem* system = Network::Get(GetScene());
    if (!system || system->IsServer()) {
        return authority == NetworkAuthority::Server || m_owner == NetPeerId::Invalid;
    }
    return authority == NetworkAuthority::Owner && m_ownedLocally;
}

bool NetworkObject::IsPredicted() const {
    const NetworkSystem* system = Network::Get(GetScene());
    if (!predicted || !system) {
        return false;
    }
    return system->IsServer() ? m_owner != NetPeerId::Invalid : m_ownedLocally;
}

void NetworkObject::Serialize(Archive& archive) {
    archive.Field("authority", authority, {.enumNames = AuthorityNames, .tooltip = "Who decides its state"});
    archive.Field("predicted", predicted, {.tooltip = "Its owner moves it ahead of the server, which corrects it"});
    archive.Field("priority", priority,
                  {.min = 0.0f, .max = 10.0f, .tooltip = "How much its changes matter when they do not all fit"});
    archive.Field("relevanceRadius", relevanceRadius,
                  {.min = 0.0f, .max = 10000.0f, .tooltip = "Beyond this distance from a player, it matters less"});
}

} // namespace Spot
