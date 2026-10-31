#include "gameplay/network/player_info.h"

#include "gameplay/core/player_controller.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(PlayerInfo);

bool PlayerInfo::IsLocal() const {
    if (Spot::Network::IsClient(GetScene())) {
        const Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
        return object && object->IsOwnedLocally();
    }
    const Spot::Entity* controller = GetScene().GetEntity(m_controller);
    return controller && controller->HasComponent<PlayerController>();
}

void PlayerInfo::Replicate(Spot::Archive& archive) {
    archive.Field("name", name);
    archive.Field("peer", peer);
    archive.Field("ping", ping);
    archive.Field("team", team);
    archive.Field("pawn", pawn);
    archive.Field("scores", scores);
}

void PlayerInfo::Serialize(Spot::Archive& archive) {
    archive.Field("name", name);
    archive.Field("team", team);
    archive.Field("scores", scores);
}

std::vector<PlayerInfo*> PlayerInfos::GetAll(Spot::Scene& scene) {
    std::vector<PlayerInfo*> players;
    scene.ForEachComponent<PlayerInfo>([&](PlayerInfo& player) { players.push_back(&player); });
    std::ranges::sort(players, [](const PlayerInfo* a, const PlayerInfo* b) {
        return a->peer != b->peer ? a->peer < b->peer : a->name < b->name;
    });
    return players;
}

PlayerInfo* PlayerInfos::GetLocal(Spot::Scene& scene) {
    for (PlayerInfo* player : GetAll(scene)) {
        if (player->IsLocal()) {
            return player;
        }
    }
    return nullptr;
}

} // namespace Gameplay
