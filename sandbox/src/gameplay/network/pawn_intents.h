#pragma once

#include <spot.h>

namespace Gameplay {

class Pawn;

// The intents of a pawn's parts (PlayerInputListener::SerializeIntent()), written on a player's machine and applied
// to the same parts of the server's copy of the pawn. Both copies are made from the same prefab, so their parts come
// in the same order: those on the pawn and below it, down to the next pawn.
namespace PawnIntents {

void Write(Pawn& pawn, Spot::BitWriter& writer);
// False when the data was wrong; the parts read before it keep what they read.
bool Read(Pawn& pawn, Spot::BitReader& reader);

} // namespace PawnIntents

} // namespace Gameplay
