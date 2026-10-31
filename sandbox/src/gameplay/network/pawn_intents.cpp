#include "gameplay/network/pawn_intents.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_input.h"

#include <vector>

namespace Gameplay {

namespace {

template <typename T>
void Collect(Spot::Entity& entity, const Pawn& pawn, std::vector<T*>& parts) {
    if (Pawn::Of(entity) != &pawn) {
        return;
    }
    entity.ForEachComponent<Spot::Component>([&](Spot::Component& component) {
        if (auto* part = dynamic_cast<T*>(&component)) {
            parts.push_back(part);
        }
    });
    for (Spot::Entity* child : entity.GetChildren()) {
        Collect(*child, pawn, parts);
    }
}

std::vector<PlayerInputListener*> GetListeners(Pawn& pawn, PawnIntents::Set set) {
    std::vector<PlayerInputListener*> listeners;
    Collect(pawn.GetEntity(), pawn, listeners);
    std::erase_if(listeners, [set](PlayerInputListener* listener) {
        switch (set) {
        case PawnIntents::Set::PerStep:
            return !listener->IsIntentPerStep();
        case PawnIntents::Set::PerFrame:
            // The view goes with frames too, as items aim where it looks as they are used.
            return dynamic_cast<PredictedPart*>(listener) != nullptr;
        case PawnIntents::Set::All:
            break;
        }
        return false;
    });
    return listeners;
}

std::vector<PredictedPart*> GetPredictedParts(Pawn& pawn) {
    std::vector<PredictedPart*> parts;
    Collect(pawn.GetEntity(), pawn, parts);
    return parts;
}

// Each in a blob of its own, so a part that reads differently spoils only itself.
template <typename T, typename F>
void WriteBlobs(const std::vector<T*>& parts, Spot::BitWriter& writer, F&& serialize) {
    writer.WriteVarUInt(parts.size());
    for (T* part : parts) {
        Spot::BitWriter blob;
        Spot::NetArchiveWriter backend(blob);
        Spot::Archive archive(backend);
        serialize(*part, archive);
        const std::vector<std::byte> bytes = blob.TakeBytes();
        writer.WriteVarUInt(bytes.size());
        writer.WriteBytes(bytes);
    }
}

template <typename T, typename F>
bool ReadBlobs(const std::vector<T*>& parts, Spot::BitReader& reader, F&& serialize) {
    const std::uint64_t count = reader.ReadVarUInt();
    for (std::uint64_t i = 0; i < count && reader.IsValid(); ++i) {
        const std::uint64_t size = reader.ReadVarUInt();
        if (!reader.IsValid() || size > reader.GetBitsLeft() / 8) {
            reader.Fail();
            break;
        }
        const std::vector<std::byte> bytes = reader.ReadBytes(static_cast<std::size_t>(size));
        if (i < parts.size()) {
            Spot::BitReader blob(bytes);
            Spot::NetArchiveReader backend(blob);
            Spot::Archive archive(backend);
            serialize(*parts[i], archive);
        }
    }
    return reader.IsValid();
}

} // namespace

namespace PawnIntents {

void Write(Pawn& pawn, Spot::BitWriter& writer, Set set) {
    WriteBlobs(GetListeners(pawn, set), writer,
               [](PlayerInputListener& listener, Spot::Archive& archive) { listener.SerializeIntent(archive); });
}

bool Read(Pawn& pawn, Spot::BitReader& reader, Set set) {
    return ReadBlobs(GetListeners(pawn, set), reader,
                     [](PlayerInputListener& listener, Spot::Archive& archive) { listener.SerializeIntent(archive); });
}

bool IsPredicted(const Pawn& pawn) {
    const Spot::NetworkObject* object = Spot::Network::GetObject(pawn.GetEntity());
    return object && object->IsPredicted();
}

void Step(Pawn& pawn, float fixedDeltaTime, bool replaying) {
    for (PredictedPart* part : GetPredictedParts(pawn)) {
        part->Step(fixedDeltaTime, replaying);
    }
}

void WriteStepState(Pawn& pawn, Spot::BitWriter& writer) {
    WriteBlobs(GetPredictedParts(pawn), writer,
               [](PredictedPart& part, Spot::Archive& archive) { part.SerializeStepState(archive); });
}

bool ReadStepState(Pawn& pawn, Spot::BitReader& reader) {
    return ReadBlobs(GetPredictedParts(pawn), reader,
                     [](PredictedPart& part, Spot::Archive& archive) { part.SerializeStepState(archive); });
}

void Corrected(Pawn& pawn, Spot::Vector3 offset) {
    for (PredictedPart* part : GetPredictedParts(pawn)) {
        part->OnCorrected(offset);
    }
}

} // namespace PawnIntents

} // namespace Gameplay
