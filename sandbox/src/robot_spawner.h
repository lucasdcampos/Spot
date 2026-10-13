#pragma once

#include <spot.h>

class RobotSpawner final : public Spot::Component {
public:
    Spot::AssetRef<Spot::Prefab> robot;
    std::vector<Spot::Vector3> positions;
    std::span<const Spot::EntityId> GetRobots() const { return m_robots; }

protected:
    void OnStart() override;
    void Serialize(Spot::Archive& archive) override;

private:
    std::vector<Spot::EntityId> m_robots;
};
