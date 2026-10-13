#include "robot_spawner.h"

SPOT_COMPONENT(RobotSpawner);

void RobotSpawner::OnStart() {
    if (!robot || !m_robots.empty()) {
        return;
    }
    for (const auto position : positions) {
        if (auto* root = GetScene().Instantiate(robot)) {
            root->GetTransform().SetWorldPosition(position);
            m_robots.push_back(root->GetId());
        }
    }
}
void RobotSpawner::Serialize(Spot::Archive& archive) {
    archive.Field("robot", robot);
    archive.Field("positions", positions);
    archive.Field("robots", m_robots, {.hidden = true});
}
