#include "gameplay/core/players.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"

namespace Gameplay::Players {

std::vector<PlayerController*> GetAll(Spot::Scene& scene) {
    std::vector<PlayerController*> players;
    scene.ForEachComponent<PlayerController>([&](PlayerController& player) { players.push_back(&player); });
    return players;
}

PlayerController* Get(Spot::Scene& scene, std::size_t index) {
    PlayerController* found = nullptr;
    std::size_t i = 0;
    scene.ForEachComponent<PlayerController>([&](PlayerController& player) {
        if (i++ == index) {
            found = &player;
        }
    });
    return found;
}

Pawn* GetPawn(Spot::Scene& scene, std::size_t index) {
    const PlayerController* player = Get(scene, index);
    return player ? player->GetPawn() : nullptr;
}

Pawn* GetBody(Spot::Scene& scene, std::size_t index) {
    Pawn* pawn = GetPawn(scene, index);
    return pawn && pawn->GetOccupant() ? pawn->GetOccupant() : pawn;
}

bool IsPlayedLocally(const Pawn& pawn) {
    bool played = false;
    pawn.GetScene().ForEachComponent<PlayerController>([&](PlayerController& player) {
        const Pawn* possessed = player.GetPawn();
        played = played || possessed == &pawn || (possessed && possessed->GetOccupant() == &pawn);
    });
    return played;
}

void RemoveAll(Spot::Scene& scene) {
    for (PlayerController* player : GetAll(scene)) {
        player->GetEntity().Destroy();
    }
}

} // namespace Gameplay::Players
