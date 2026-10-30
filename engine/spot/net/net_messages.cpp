#include "spot/net/net_messages.h"

namespace Spot {

bool NetMessageHandlers::Dispatch(NetPeerId peer, std::span<const std::byte> data,
                                  const NetEntityMapper* entities) const {
    BitReader reader(data);
    return Dispatch(peer, reader, entities);
}

bool NetMessageHandlers::Dispatch(NetPeerId peer, BitReader& reader, const NetEntityMapper* entities) const {
    const auto id = NetMessageId{reader.ReadBits(32)};
    if (!reader.IsValid()) {
        return false;
    }
    const auto found = m_handlers.find(id);
    return found != m_handlers.end() && found->second(peer, reader, entities);
}

} // namespace Spot
