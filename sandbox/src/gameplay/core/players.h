#pragma once

#include "gameplay/core/pawn.h"

#include <spot.h>

#include <cstddef>
#include <vector>

namespace Gameplay {

class PlayerController;

// The local players of a scene, for what serves them rather than one of their pawns: the HUD, menus, console commands.
namespace Players {

// Their controllers, enabled and active, in the order they were added.
std::vector<PlayerController*> GetAll(Spot::Scene& scene);
// Null when there are not that many.
PlayerController* Get(Spot::Scene& scene, std::size_t index = 0);
// The pawn that player possesses; null when there is no such player or it possesses nothing.
Pawn* GetPawn(Spot::Scene& scene, std::size_t index = 0);
// The player's own pawn while riding another one, otherwise the pawn possessed.
Pawn* GetBody(Spot::Scene& scene, std::size_t index = 0);
// Whether a local player plays as the pawn, possessing it or riding in what it possesses: what this machine shows
// from the pawn's eyes, as its first-person hands, rather than of it.
bool IsPlayedLocally(const Pawn& pawn);
// Destroys the scene's local players before it starts, so its mode gives them no pawns: a dedicated server's scene,
// which only remote players play in.
void RemoveAll(Spot::Scene& scene);

template <typename T>
T* FindComponent(Spot::Scene& scene, std::size_t index = 0) {
    Pawn* body = GetBody(scene, index);
    return body ? body->GetEntity().GetComponent<T>() : nullptr;
}

} // namespace Players

} // namespace Gameplay
