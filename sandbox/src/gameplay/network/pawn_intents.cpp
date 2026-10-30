#include "gameplay/network/pawn_intents.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_input.h"

#include <vector>

namespace Gameplay {

namespace {

void Collect(Spot::Entity& entity, const Pawn& pawn, std::vector<PlayerInputListener*>& listeners) {
    if (Pawn::Of(entity) != &pawn) {
        return;
    }
    entity.ForEachComponent<Spot::Component>([&](Spot::Component& component) {
        if (auto* listener = dynamic_cast<PlayerInputListener*>(&component)) {
            listeners.push_back(listener);
        }
    });
    for (Spot::Entity* child : entity.GetChildren()) {
        Collect(*child, pawn, listeners);
    }
}

std::vector<PlayerInputListener*> GetListeners(Pawn& pawn) {
    std::vector<PlayerInputListener*> listeners;
    Collect(pawn.GetEntity(), pawn, listeners);
    return listeners;
}

} // namespace

namespace PawnIntents {

void Write(Pawn& pawn, Spot::BitWriter& writer) {
    const std::vector<PlayerInputListener*> listeners = GetListeners(pawn);
    writer.WriteVarUInt(listeners.size());
    for (PlayerInputListener* listener : listeners) {
        // Each in a blob of its own, so a part that reads differently spoils only itself.
        Spot::BitWriter blob;
        Spot::NetArchiveWriter backend(blob);
        Spot::Archive archive(backend);
        listener->SerializeIntent(archive);
        const std::vector<std::byte> bytes = blob.TakeBytes();
        writer.WriteVarUInt(bytes.size());
        writer.WriteBytes(bytes);
    }
}

bool Read(Pawn& pawn, Spot::BitReader& reader) {
    const std::vector<PlayerInputListener*> listeners = GetListeners(pawn);
    const std::uint64_t count = reader.ReadVarUInt();
    for (std::uint64_t i = 0; i < count && reader.IsValid(); ++i) {
        const std::uint64_t size = reader.ReadVarUInt();
        if (!reader.IsValid() || size > reader.GetBitsLeft() / 8) {
            reader.Fail();
            break;
        }
        const std::vector<std::byte> bytes = reader.ReadBytes(static_cast<std::size_t>(size));
        if (i < listeners.size()) {
            Spot::BitReader blob(bytes);
            Spot::NetArchiveReader backend(blob);
            Spot::Archive archive(backend);
            listeners[i]->SerializeIntent(archive);
        }
    }
    return reader.IsValid();
}

} // namespace PawnIntents

} // namespace Gameplay
