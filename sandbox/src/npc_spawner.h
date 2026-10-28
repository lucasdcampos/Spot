#pragma once

#include <spot.h>

struct NpcSpawn {
    Spot::AssetRef<Spot::Prefab> prefab;
    Spot::Vector3 position;
    void Serialize(Spot::Archive& archive);
};

class NpcSpawner final : public Spot::Component {
public:
    std::vector<NpcSpawn> spawns;
    std::span<const Spot::EntityId> GetNpcs() const { return m_npcs; }

protected:
    void OnStart() override;
    void Serialize(Spot::Archive& archive) override;

private:
    std::vector<Spot::EntityId> m_npcs;
};
