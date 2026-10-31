#pragma once

#include "spot/core/debug/assert.h"
#include "spot/net/net_archive.h"
#include "spot/net/net_transport.h"

#include <concepts>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Spot {

// Identifies a type of message on the wire: the hash of its name, so it is the same in every build.
enum class NetMessageId : std::uint32_t {};

constexpr NetMessageId GetNetMessageId(std::string_view name) {
    std::uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    return NetMessageId{hash};
}

// A message is a struct with a name to know it by, and its fields described as for saving:
//
//     struct ChatMessage {
//         static constexpr std::string_view NetName = "Chat";
//         std::string text;
//         void Serialize(Spot::Archive& archive) { archive.Field("text", text); }
//     };
template <typename T>
concept NetMessage = ArchiveSerializable<T> && std::default_initializable<T> && requires {
    { T::NetName } -> std::convertible_to<std::string_view>;
};

// The message's id, then its fields.
template <NetMessage T>
void WriteNetMessage(BitWriter& writer, T& message, const NetEntityMapper* entities = nullptr) {
    writer.WriteBits(static_cast<std::uint32_t>(GetNetMessageId(T::NetName)), 32);
    WriteArchived(writer, message, entities);
}

template <NetMessage T>
std::vector<std::byte> EncodeNetMessage(T message, const NetEntityMapper* entities = nullptr) {
    BitWriter writer;
    WriteNetMessage(writer, message, entities);
    return writer.TakeBytes();
}

// Calls a function for each type of message received, by its id. A type has one handler: a new one replaces it.
class NetMessageHandlers {
public:
    template <NetMessage T>
    void On(std::function<void(NetPeerId, T&)> handler) {
        const NetMessageId id = GetNetMessageId(T::NetName);
        const auto [name, added] = m_names.try_emplace(id, T::NetName);
        SPOT_ASSERT(added || name->second == T::NetName, "Two messages of different names have the same id");
        m_handlers[id] = [handler = std::move(handler)](NetPeerId peer, BitReader& reader,
                                                        const NetEntityMapper* entities) {
            T message;
            if (!ReadArchived(reader, message, entities)) {
                return false;
            }
            handler(peer, message);
            return true;
        };
    }

    // False, calling nothing, when no handler knows the message or its data is wrong.
    bool Dispatch(NetPeerId peer, std::span<const std::byte> data, const NetEntityMapper* entities = nullptr) const;
    // Reads one message from where the reader is, for packets that hold several.
    bool Dispatch(NetPeerId peer, BitReader& reader, const NetEntityMapper* entities = nullptr) const;

private:
    std::unordered_map<NetMessageId, std::function<bool(NetPeerId, BitReader&, const NetEntityMapper*)>> m_handlers;
    std::unordered_map<NetMessageId, std::string_view> m_names;
};

} // namespace Spot
