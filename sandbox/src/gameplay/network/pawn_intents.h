#pragma once

#include <spot.h>

namespace Gameplay {

class Pawn;

// The intents of a pawn's parts (PlayerInputListener::SerializeIntent()), written on a player's machine and applied
// to the same parts of the server's copy of the pawn. Both copies are made from the same prefab, so their parts come
// in the same order: those on the pawn and below it, down to the next pawn.
namespace PawnIntents {

// Which parts' intents. A predicted pawn's player sends each fixed step's for its predicted parts and the view they go
// along (PlayerInputListener::IsIntentPerStep()), and each frame's for the others; any other pawn's, all each frame.
enum class Set {
    All,
    PerStep,
    PerFrame,
};

void Write(Pawn& pawn, Spot::BitWriter& writer, Set set = Set::All);
// False when the data was wrong; the parts read before it keep what they read.
bool Read(Pawn& pawn, Spot::BitReader& reader, Set set = Set::All);

// Whether the pawn's network object is predicted where this runs (Spot::NetworkObject::IsPredicted()).
bool IsPredicted(const Pawn& pawn);
// Steps its predicted parts (PredictedPart) by one fixed step.
void Step(Pawn& pawn, float fixedDeltaTime, bool replaying);
// The state its predicted parts are in, and puts them back in such a state.
void WriteStepState(Pawn& pawn, Spot::BitWriter& writer);
bool ReadStepState(Pawn& pawn, Spot::BitReader& reader);
// Tells its predicted parts a correction moved it by the offset (PredictedPart::OnCorrected()).
void Corrected(Pawn& pawn, Spot::Vector3 offset);

} // namespace PawnIntents

} // namespace Gameplay
