#include "npc_spawner.h"

SPOT_COMPONENT(NpcSpawner);

void NpcSpawn::Serialize(Spot::Archive& archive) {
    archive.Field("prefab", prefab);
    archive.Field("position", position);
}

void NpcSpawner::OnStart() {
    // Clients are shown the server's NPCs.
    if (!m_npcs.empty() || !Spot::Network::IsServer(GetScene())) {
        return;
    }
    for (const auto& spawn : spawns) {
        if (auto* root = GetScene().Instantiate(spawn.prefab)) {
            root->GetTransform().SetWorldPosition(spawn.position);
            m_npcs.push_back(root->GetId());
        }
    }
}
void NpcSpawner::Serialize(Spot::Archive& archive) {
    archive.Field("spawns", spawns);
    archive.Field("npcs", m_npcs, {.hidden = true});
}
